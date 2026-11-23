# ESP32 Classic OTA-Übung

Der ESP32 gibt bei 115200 Baud einmal pro Sekunde `hello world 1` aus. Mit eingerichtetem WLAN und synchronisierter Uhr prüft ein eigener Task ungefähr alle 60 Sekunden das aktuelle GitHub Release. Der erste Check erfolgt im ersten Durchlauf mit WLAN und gültiger Uhrzeit.

Die Firmware liest zunächst den ESP-IDF-Image-Header mit Projektname und Versionsnummer. Bei gleicher oder kleinerer Version beendet sie die Verbindung nach dem Header. Eine höhere Version lädt sie in einen freien OTA-Slot. ESP-IDF prüft die ECDSA-Signatur, bevor es den Slot zum Start auswählt. Bei einem unvollständigen oder ungültig signierten Download bleibt die laufende Firmware aktiv. Die Prüfroutine verwendet den in der Firmware hinterlegten öffentlichen Schlüssel. Hardware Secure Boot und Flash-Verschlüsselung sind für diese Übung deaktiviert. [Espressif zur Signaturprüfung bei OTA](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32/security/secure-boot-v1.html#signed-app-verification-without-hardware-secure-boot)

## 1. Build-Umgebung und Schlüssel einrichten

Vorausgesetzt werden ein ESP32 Classic mit mindestens 4 MiB Flash, ein USB-Datenkabel, ein 2,4-GHz-WLAN und ESP-IDF **v5.4.4** für Windows. Öffne die ESP-IDF-PowerShell und wechsle in den Projektordner. Ersetze in den folgenden Befehlen `COM5` durch den Port deines Boards. [ESP-IDF installieren](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32/get-started/index.html)

```powershell
idf.py --version
idf.py set-target esp32
New-Item -ItemType Directory -Force keys
if (-not (Test-Path keys/ota-signing-key.pem)) {
    python -m espsecure generate_signing_key --version 1 keys/ota-signing-key.pem
}
```

Der private Schlüssel wird einmal erzeugt. Bewahre ihn mit einer privaten Sicherung auf und verwende ihn für alle Releases. ESP-IDF leitet daraus den öffentlichen Prüfschlüssel ab und signiert die App automatisch bei `idf.py build`. Der Ordner `keys/` ist in `.gitignore` eingetragen.

## 2. Version 1 per USB installieren

Sobald das öffentliche Repository existiert, öffne `idf.py menuconfig` und setze unter **OTA demo → GitHub Release firmware URL** die spätere Download-Adresse. Die Einstellung liegt lokal in `sdkconfig` und muss bei jedem Release-Build dieselbe sein.

```text
https://github.com/DEIN-KONTO/DEIN-REPO/releases/latest/download/firmware.bin
```

Mit dem voreingestellten Platzhalter kannst du die serielle Ausgabe bereits ausprobieren. Wenn du die echte URL später einstellst, flashe Version 1 erneut per USB, damit der ESP32 die Adresse kennt.

```powershell
idf.py build
idf.py -p COM5 flash
```

Beim Wiederholen auf einem Board, auf dem diese Demo bereits per OTA aktualisiert wurde, kann der Bootloader weiterhin den zuletzt gewählten OTA-Slot starten. Für einen kompletten Neustart der Übung kannst du vor dem Flashen `idf.py -p COM5 erase-flash` ausführen. Das löscht den gesamten Flash inklusive der WLAN-Daten, die du dann erneut einrichtest.

## 3. WLAN einmalig per USB einrichten

Lege im Projektordner eine lokale Datei `wifi-nvs.csv` in UTF-8 ohne BOM mit diesem Inhalt an. Ersetze die Beispielwerte. Enthält ein Wert ein Komma, setze ihn in doppelte Anführungszeichen. Verdopple doppelte Anführungszeichen innerhalb eines so eingeschlossenen Werts. Für ein offenes WLAN bleibt das vierte Feld in der Passwortzeile leer.

```csv
key,type,encoding,value
wifi,namespace,,
ssid,data,string,MEIN-WLAN
password,data,string,MEIN-PASSWORT
```

```powershell
python "$env:IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" generate wifi-nvs.csv wifi-nvs.bin 0x4000
python -m esptool --chip esp32 --port COM5 write_flash 0x3d0000 wifi-nvs.bin
idf.py -p COM5 monitor
```

Die Adresse `0x3d0000` ist der Anfang der Partition `credentials` in `partitions.csv`. OTA-Updates schreiben ausschließlich in eine App-Partition. Die WLAN-Daten bleiben auf dem Gerät und in den lokalen, ignorierten WLAN-Dateien. Im Monitor solltest du jede Sekunde `hello world 1` sehen. Mit **Strg+]** beendest du den Monitor. [Espressif zum NVS-Generator](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/storage/nvs_partition_gen.html)

## 4. Version 2 bauen und signieren

1. Ändere den Inhalt von `version.txt` von `1` auf `2`. Verwende für jedes weitere Release eine größere ganze Zahl ohne führende Nullen.
2. Starte den Build im gleichen Projektordner mit unveränderter Release-URL und demselben Schlüssel.

   ```powershell
   idf.py build
   ```

3. Kopiere das signierte App-Image unter dem festen Namen des Release-Assets.

   ```powershell
   Copy-Item build/test_01_otademo.bin build/firmware.bin
   python -m espsecure verify_signature --version 1 --keyfile keys/ota-signing-key.pem build/firmware.bin
   ```

   Führe Kopie und Prüfung erst nach erfolgreichem Build aus. Veröffentliche die Datei nach erfolgreicher Signaturprüfung. Der ESP32 liest seine Versionsnummer aus der App-Datei. Der Git-Tag dient der Zuordnung des Releases zum Quellcode.

## 5. Auf GitHub veröffentlichen und OTA prüfen

1. Committe und pushe den passenden Quellstand ins öffentliche GitHub-Repository.
2. Öffne dort **Releases → Draft a new release**, lege den Tag `v2` für diesen Commit an und lade `build/firmware.bin` als Release-Asset hoch.
3. Veröffentliche das Release als reguläres Release und markiere es als **Latest**. Die in der Firmware konfigurierte URL muss jetzt das Asset `firmware.bin` liefern. GitHub leitet die Anfrage für diese feste URL zum Asset des aktuellen Releases um. [GitHub zu permanenten Release-Asset-Links](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases)
4. Lasse den ESP32 mit Version 1 laufen und beobachte den Monitor. Beim nächsten Prüflauf nach WLAN-Verbindung und Uhrzeitsynchronisation startet der Download. Nach erfolgreicher Installation startet der ESP32 neu und gibt `hello world 2` aus. Nach einem weiteren Prüflauf sollte die Ausgabe bei Version 2 bleiben.

Wenn Version 1 weiterläuft, prüfe Download-Adresse, WLAN und den Zugang zu `pool.ntp.org` über UDP-Port 123. Für Diagnoseausgaben kannst du unter **Component config → Log → Default log verbosity** vorübergehend **Info** einstellen und mit dieser Einstellung per USB flashen. Ohne diese Diagnoseeinstellung besteht die Anwendungsausgabe aus den `hello world`-Zeilen. Beim Reset können Meldungen von ROM und Bootloader davor erscheinen.
