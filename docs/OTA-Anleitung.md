# ESP32 Classic OTA-Übung mit PlatformIO und Arduino

Der ESP32 gibt bei 115200 Baud einmal pro Sekunde `hello world 1` aus. Mit WLAN, gültiger Uhrzeit und eingerichtetem Prüfschlüssel fragt ein eigener Task alle 60 Sekunden das aktuelle GitHub Release ab. Der HTTP-Download beginnt mit einem kurzen Versionsheader. Für eine höhere Version lädt der ESP32 das App-Image in den freien OTA-Slot und prüft die ECDSA-P-256-Signatur über Versionsheader und Image. Erst nach erfolgreicher Prüfung wählt er den neuen Slot und startet neu. Bei einem abgebrochenen Download oder einer ungültigen Signatur läuft die bisherige Firmware weiter.

## 1. PlatformIO und Schlüssel einrichten

Öffne den Projektordner in VS Code mit der **PlatformIO IDE**-Erweiterung. Verwende ein ESP32-Classic-Board mit mindestens 4 MiB Flash, ein USB-Datenkabel und ein 2,4-GHz-WLAN. Das Projekt verwendet das PlatformIO-Board `esp32dev` und `framework = arduino`. Die Build- und Flash-Werkzeuge verwaltet PlatformIO. [PlatformIO für Espressif 32](https://docs.platformio.org/en/latest/platforms/espressif32.html)

Öffne in VS Code ein **PlatformIO Core CLI**-Terminal und führe im Projektordner aus:

```powershell
pio run -t ota-key
```

Der Befehl erzeugt einmalig `keys/ota-signing-key.pem` und leitet daraus den öffentlichen Prüfschlüssel `keys/ota-public-key.bin` ab. Falls du schon einen ECDSA-Schlüssel aus der bisherigen Demo hast, verwende dieselbe PEM-Datei an dieser Stelle. Sichere den privaten Schlüssel separat und verwende ihn für alle Firmware-Versionen. Das Build-Skript bindet nur den öffentlichen Schlüssel ins Image ein. Der gesamte Ordner `keys/` bleibt lokal.

## 2. Version 1 per USB installieren

Trage in `platformio.ini` bei `custom_ota_url` die Adresse des späteren Assets in deinem öffentlichen GitHub-Repository ein:

```text
https://github.com/DEIN-KONTO/DEIN-REPO/releases/latest/download/firmware.bin
```

Mit der Platzhalter-URL kannst du die serielle Ausgabe bereits testen. Sobald du die echte Adresse einträgst, flashe Version 1 mit der neuen Einstellung erneut per USB. Ersetze in den Befehlen `COM5` durch den Port deines Boards.

```powershell
pio run
pio run -t upload --upload-port COM5
```

Für den Wechsel von der bisherigen ESP-IDF-Demo ist ein USB-Upload erforderlich. Der Upload installiert den Arduino-Bootloader, die Partitionstabelle und das erste App-Image. Bei Bedarf setzt `pio run -t erase --upload-port COM5` den gesamten Flash vor dem Upload zurück. Dabei werden auch die WLAN-Daten gelöscht.

## 3. WLAN einmalig per USB einrichten

Lege lokal im Projektordner `wifi-nvs.csv` als UTF-8 ohne BOM an und ersetze die Beispielwerte. Ein Wert mit Komma gehört in doppelte Anführungszeichen. Verdopple doppelte Anführungszeichen innerhalb eines so eingeschlossenen Werts. Für ein offenes WLAN bleibt das Passwortfeld leer.

```csv
key,type,encoding,value
wifi,namespace,,
ssid,data,string,MEIN-WLAN
password,data,string,MEIN-PASSWORT
```

Installiere den eigenständigen NVS-Generator einmalig. Die folgenden Befehle verwenden unter Windows das Python aus der üblichen PlatformIO-Installation:

```powershell
$pioPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$pioEsptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptool\esptool.exe'
& $pioPython -m pip install esp-idf-nvs-partition-gen
& $pioPython -m esp_idf_nvs_partition_gen generate wifi-nvs.csv wifi-nvs.bin 0x4000
& $pioEsptool --chip esp32 --port COM5 write-flash 0x3d0000 wifi-nvs.bin
pio device monitor --port COM5 --baud 115200
```

`0x3d0000` ist der Anfang der separaten Partition `credentials`. Die Zugangsdaten bleiben bei OTA-Updates erhalten. Beide WLAN-Dateien werden von Git ignoriert. Im Monitor sollte jede Sekunde `hello world 1` erscheinen. [Espressifs eigenständiger NVS-Generator](https://github.com/espressif/esp-idf-nvs-partition-gen)

## 4. Version 2 bauen und signieren

1. Ändere den Inhalt von `version.txt` von `1` auf `2`. Verwende bei jedem weiteren Update eine höhere ganze Zahl ohne führende Nullen.
2. Baue und signiere mit demselben Schlüssel und derselben Release-URL:

   ```powershell
   pio run -t ota-release
   ```

3. Der Befehl prüft die Signatur mit dem öffentlichen Schlüssel. Verwende nach erfolgreichem Lauf die Datei **`.pio/build/esp32dev/ota/firmware.bin`** als Release-Asset. PlatformIO verwendet für USB-Uploads die Datei **`.pio/build/esp32dev/firmware.bin`**. Das Release-Asset enthält zusätzlich den Versionsheader und die Signatur.

## 5. Auf GitHub veröffentlichen und OTA prüfen

1. Committe und pushe den zu Version 2 gehörenden Quellstand.
2. Öffne im öffentlichen Repository **Releases → Draft a new release**. Lege den Tag `v2` für diesen Commit an und lade **`.pio/build/esp32dev/ota/firmware.bin`** als Asset hoch.
3. Veröffentliche das Release als **Latest**. Die feste Adresse aus `platformio.ini` führt jetzt zu diesem Asset. [GitHub zu Release-Asset-Links](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases)
4. Lass Version 1 auf dem Board laufen. Nach WLAN-Verbindung und Uhrzeitsynchronisation sollte das Board beim nächsten erfolgreichen Check das Update installieren, neu starten und `hello world 2` ausgeben.

Wenn Version 1 weiterläuft, öffne die Release-URL ohne GitHub-Anmeldung und prüfe, ob `firmware.bin` heruntergeladen wird. Prüfe außerdem das WLAN, den Zugang zu `pool.ntp.org` über UDP-Port 123 und ob beide Builds denselben Prüfschlüssel verwenden. Beim Reset können ROM und Bootloader eigene Meldungen ausgeben.
