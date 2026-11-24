# ESP32 Classic OTA Demo with PlatformIO and Arduino

(Created mostly with help from ChatGPT, though every now and then I got to make and comment on an improvement of my own. ;-)

The ESP32 prints `hello world 1` once per second at 115200 baud. With Wi-Fi, a valid system clock, and a verification key configured, `loop()` checks the latest GitHub release every 30 seconds. Regular output pauses during the check and any download. The HTTP download starts with a short version header. If the version is higher than the installed one, the ESP32 downloads the app image into the inactive OTA slot and verifies the ECDSA P-256 signature over the version header and the image. After successful verification, it selects the new slot and restarts. If the download is interrupted or the signature is invalid, the current firmware keeps running.

The ESP32 reads `version-latest.txt` first. If the number is equal to or lower than the installed version, invalid, or unavailable, the ESP32 skips the request for `firmware.bin`. If the number is higher, the release asset header must contain that same newer version. This keeps an uploaded release from being installed until you approve it in `version-latest.txt`.

## 1. Set up PlatformIO and the signing key

Open the project folder in VS Code with the **PlatformIO IDE** extension. Use an ESP32 Classic board with at least 4 MiB of flash, a USB data cable, and a 2.4 GHz Wi-Fi network. The project uses the PlatformIO board `esp32dev` and `framework = arduino`. PlatformIO manages the build and upload tools. [PlatformIO for Espressif 32](https://docs.platformio.org/en/latest/platforms/espressif32.html)

Open a **PlatformIO Core CLI** terminal in VS Code and run this command from the project folder.

```powershell
pio run -t ota-key
```

The command creates `keys/ota-signing-key.pem` once and derives the public verification key `keys/ota-public-key.bin` from it. If you already have an ECDSA signing key from this demo, reuse that PEM file at the same path. Keep a separate backup of the private key and use it for all firmware versions. The build script embeds only the public key in the image. The entire `keys/` folder stays local.

## 2. Install version 1 over USB

In `platformio.ini`, set `custom_ota_url` to the address of the asset you will publish in your public GitHub repository. Replace `DEIN-KONTO` and `DEIN-REPO` with your GitHub account and repository names.

```text
https://github.com/DEIN-KONTO/DEIN-REPO/releases/latest/download/firmware.bin
```

Set `custom_ota_latest_version_url` to the direct raw URL of `version-latest.txt` at the root of the public branch. For example:

```text
https://raw.githubusercontent.com/DEIN-KONTO/DEIN-REPO/main/version-latest.txt
```

The file contains a single decimal version number. The initial value `0` means no OTA release has been approved yet. `version.txt` still controls the build and can be updated independently.

You can test the serial output with the placeholder URL. Once you enter the real address, flash version 1 over USB again so the board picks up the new setting. Replace `COM5` in the commands with your board's port.

```powershell
pio run
pio run -t upload --upload-port COM5
```

The initial upload via USB or a UART adapter installs the Arduino bootloader, the partition table, and the first app image. If you need to reset the board before uploading, `pio run -t erase --upload-port COM5` erases the entire flash, including the Wi-Fi credentials. You can also use the PlatformIO Upload button in VS Code for the initial upload. ;-)

## 3. Provision Wi-Fi over USB

Create a local file named `wifi-nvs.csv` in the project folder, save it as UTF-8 without a BOM, and replace the example values. Enclose any value that contains a comma in double quotes. Double any quote characters inside a quoted value. For an open network, leave the password field empty.

```csv
key,type,encoding,value
wifi,namespace,,
ssid,data,string,MEIN-WLAN
password,data,string,MEIN-PASSWORT
```

Install the standalone NVS generator once. The commands below use the Python executable from a standard Windows PlatformIO installation.

```powershell
$pioPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$pioEsptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptool\esptool.exe'
& $pioPython -m pip install esp-idf-nvs-partition-gen
& $pioPython -m esp_idf_nvs_partition_gen generate wifi-nvs.csv wifi-nvs.bin 0x4000
& $pioEsptool --chip esp32 --port COM5 write-flash 0x3d0000 wifi-nvs.bin
pio device monitor --port COM5 --baud 115200
```

`0x3d0000` is the start of the separate `credentials` partition. OTA updates preserve the credentials, and Git ignores both local Wi-Fi files. The monitor prints `hello world 1` once per second. When the board tries to connect, it shows the SSID and whether a password is set. Once connected, it prints the IP address. If it fails to connect within 15 seconds or an existing connection drops, it prints `WiFi not connected`. You can test Wi-Fi with the placeholder URL. [Espressif's standalone NVS generator](https://github.com/espressif/esp-idf-nvs-partition-gen)

(This is a slightly roundabout way to get the Wi-Fi credentials onto the ESP32, but it keeps the SSID and password separate from the code. Every ESP32 can receive the same `.bin` file, regardless of which Wi-Fi network it uses. It was also a good chance to learn how to flash individual NVS settings onto each ESP32. In real applications I prefer a different way to provision Wi-Fi credentials, but this method would be handy for giving each ESP32 in a house or camper van its own name or number during initial setup and storing it in NVS. ;-)

## 4. Build and sign version 2

1. Change `version.txt` from `1` to `2`. Use a higher integer without leading zeros for each subsequent update.
2. Build and sign with the same key and release URL.

   ```powershell
   pio run -t ota-release
   ```

3. The command verifies the signature using the public key. It creates **`firmware.bin`** and **`build-info.json`** in **`.pio/build/esp32dev/ota/`** for the release. The JSON file contains the firmware version and the app image's UTC build timestamp. PlatformIO uses **`.pio/build/esp32dev/firmware.bin`** for USB uploads. The release image also includes the version header and signature.

## 5. Publish on GitHub and test OTA

1. You can commit and push the source code for version 2, or use a simple README commit in the public repository.
2. In the public repository, open **Releases → Draft a new release**. Create the tag `v2` for that commit, enter a release title, and attach **`firmware.bin`** and **`build-info.json`** from **`.pio/build/esp32dev/ota/`** as release assets.
3. Publish the release as **Latest**. The fixed URL in `platformio.ini` now points to the new asset. [GitHub documentation on release asset links](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases)
4. Once both assets are available and you are ready to approve version 2, set `version-latest.txt` to `2` in the public repository. Commit and push the file to the branch used by the configured raw URL. The build and GitHub upload do not update this file automatically.
5. Leave version 1 running on the board. On its next successful update check, the board installs the update, restarts, and prints `hello world 2`.

Each `hello world` line starts with the UTC time, for example `14:32:08 UTC hello world 2`. Until the clock has synced, the line starts with the time since boot, for example `uptime 5s hello world 2`.

When newer firmware is available, the monitor also shows the Git tag, release title, and build date. If the offered firmware version is equal to or older than the installed one, it prints `OTA: no newer version`. The ESP32 looks up metadata for the tag that GitHub's download URL redirects to. It refreshes the cache after five minutes or when the version or tag changes. For older releases without `build-info.json`, the build date appears as `unavailable`.

If the release has a description, the monitor also prints the first 255 characters of the release notes. The preview shows GitHub's Markdown as written and preserves line breaks.

The monitor shows HTTP status codes, the available version, the signature verification result, and the restart. Before each GET request, `GET line` shows the length of its request line, including query parameters. `line buffer minimum` includes the terminating null byte. The configured buffer also needs room for HTTP headers and for variations in the length of GitHub redirects.

If version 1 keeps running, open the release URL while signed out of GitHub and check that it downloads `firmware.bin`. Also check Wi-Fi, access to `pool.ntp.org` over UDP port 123, and whether both builds use the same public verification key. The ROM and bootloader may print their own messages during reset.

## Reuse the modules in another Arduino project

- `src/wifi_connection.h` and `src/wifi_connection.cpp` read the Wi-Fi settings from `credentials` and connect the ESP32. Call `WifiConnection::begin()` in setup and `WifiConnection::reportStatus()` in the loop. This provisioning setup requires an NVS partition named `credentials` and a namespace named `wifi` in the target project.
- `src/ota_update.h` and `src/ota_update.cpp` provide the three synchronous OTA calls below. The target project needs two OTA app slots and an `otadata` partition. Call `OtaUpdate::begin()` once before using OTA, then wait for Wi-Fi and the system clock. The demo reserves a larger Arduino loop stack with `SET_LOOP_TASK_STACK_SIZE(10240)`.
- `main.cpp` shows how to wire everything together with Arduino setup, a 30-second check interval, clock synchronization, and serial output. To use the same release build process, also copy `scripts/ota_release.py`, `version.txt`, and the corresponding settings from `platformio.ini`.

To use the module in a real-world application, follow these steps.

1. Call `OtaUpdate::quickCheck(latestVersionUrl, installedVersion)` on your schedule. It returns the newer published version, or `0` if no update was found or the check failed.
2. Call `OtaUpdate::getReleaseDetails(firmwareUrl, installedVersion, publishedVersion, details)` to check the asset's header and fill an `OtaUpdate::ReleaseDetails` object. The call returns `true` when the header matches. Unavailable metadata fields stay empty. This step never writes to flash. The demo prints the returned details over serial.
3. Call `OtaUpdate::installUpdate(details, installedVersion, publicKey)` when installation should begin. It reopens the selected asset, rechecks the version, then downloads and verifies the update. It returns the version selected for boot, or `0` on failure. The caller controls the restart. If installation is deferred and you want to honor a changed release approval, repeat the quick check and preview before installing.

The demo runs all three steps automatically. Another application can notify the user after step 1 and wait for confirmation after step 2.
