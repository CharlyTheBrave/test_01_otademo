from pathlib import Path
import json
import os
import re
import struct
import subprocess

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
build = Path(env.subst("$BUILD_DIR")).resolve()
keys = project / "keys"
private_key = keys / "ota-signing-key.pem"
public_key = keys / "ota-public-key.bin"
version_text = (project / "version.txt").read_text(encoding="ascii").strip()
if not re.fullmatch(r"[1-9][0-9]{0,9}", version_text) or int(version_text) > 0xFFFFFFFF:
    raise ValueError("version.txt must contain an integer from 1 to 4294967295")
version = int(version_text)
url = env.GetProjectOption("custom_ota_url")
if not url.startswith("https://"):
    raise ValueError("custom_ota_url must use HTTPS")

public_bytes = public_key.read_bytes() if public_key.exists() else b""
if public_key.exists() and len(public_bytes) != 64:
    raise ValueError("Expected a 64-byte ECDSA P-256 public key from ota-key")

header = build / "ota_config.h"
key_bytes = ", ".join(hex(byte) for byte in public_bytes)
config = (
    "#pragma once\n"
    f"constexpr uint32_t firmwareVersion = {version}u;\n"
    f"constexpr char otaUrl[] = {json.dumps(url)};\n"
    f"constexpr bool hasOtaKey = {'true' if public_bytes else 'false'};\n"
    f"constexpr uint8_t otaPublicKey[65] = {{0x04{', ' + key_bytes if key_bytes else ''}}};\n"
)
if not header.exists() or header.read_text(encoding="ascii") != config:
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text(config, encoding="ascii")
env.Append(CPPPATH=[str(build)])


def espsecure(*args):
    package = Path(env.PioPlatform().get_package_dir("tool-esptool"))
    executable = package / ("espsecure.exe" if os.name == "nt" else "espsecure")
    subprocess.run([str(executable), *map(str, args)], check=True)


def prepare_key(source, target, env):
    keys.mkdir(exist_ok=True)
    if not private_key.exists():
        espsecure("generate-signing-key", "--version", "1", private_key)
    temporary = public_key.with_suffix(".tmp")
    espsecure("extract-public-key", "--version", "1", "--keyfile", private_key, temporary)
    temporary.replace(public_key)


def sign_release(source, target, env):
    if not private_key.is_file() or not public_bytes:
        raise ValueError("Run pio run -t ota-key first")
    output = build / "ota"
    output.mkdir(exist_ok=True)
    unsigned = output / "unsigned.bin"
    temporary = output / "signed.tmp"
    image = Path(str(source[0])).read_bytes()
    unsigned.write_bytes(struct.pack("<4sI", b"OTA1", version) + image)
    espsecure("sign-data", "--version", "1", "--keyfile", private_key, "--output", temporary, unsigned)
    espsecure("verify-signature", "--version", "1", "--keyfile", public_key, temporary)
    release = output / "firmware.bin"
    temporary.replace(release)
    print(f"Signed OTA release {version}: {release}")


env.AddCustomTarget("ota-key", None, prepare_key, title="Prepare OTA signing key")
env.AddCustomTarget("ota-release", "$BUILD_DIR/firmware.bin", sign_release, title="Build and sign OTA release")
