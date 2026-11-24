# ESP32 Classic OTA-Übung mit PlatformIO und Arduino
(grossteils mit Unterstützung durch ChatGpt erstellt - aber ab und zu durfte ich noch eine eigene Verbesserung einfügen und kommentieren ;-)

Der ESP32 gibt bei 115200 Baud einmal pro Sekunde `hello world 1` aus. Mit WLAN, gültiger Uhrzeit und eingerichtetem Prüfschlüssel fragt `loop()` alle 30 Sekunden das aktuelle GitHub Release ab. Während der synchronen Prüfung und eines Downloads pausiert die reguläre Ausgabe. Der HTTP-Download beginnt mit einem kurzen Versionsheader. Für eine höhere Version lädt der ESP32 das App-Image in den freien OTA-Slot und prüft die ECDSA-P-256-Signatur über Versionsheader und Image. Erst nach erfolgreicher Prüfung wählt er den neuen Slot und startet neu. Bei einem abgebrochenen Download oder einer ungültigen Signatur läuft die bisherige Firmware weiter.

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

Der initiale upload zum ESP32 über ein USB-Kabel oder einen UART-Adapter installiert den Arduino-Bootloader, die Partitionstabelle und das erste App-Image. Bei Bedarf setzt `pio run -t erase --upload-port COM5` den gesamten Flash vor dem Upload zurück. Dabei werden auch die WLAN-Daten gelöscht.
(Statt des Kommandos kann man in VS Code mit PlatformIO wohl auch einfach den upload-button für den initial upload verwenden ;-)

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

`0x3d0000` ist der Anfang der separaten Partition `credentials`. Die Zugangsdaten bleiben bei OTA-Updates erhalten. Beide WLAN-Dateien werden von Git ignoriert. Im Monitor erscheint jede Sekunde `hello world 1`. Beim Verbindungsversuch siehst du die verwendete SSID und ob ein Passwort gesetzt ist. Bei erfolgreicher Verbindung erscheint die IP-Adresse. Falls nach 15 Sekunden keine Verbindung besteht oder eine bestehende Verbindung abbricht, erscheint `WiFi not connected`. Der WLAN-Test funktioniert auch mit der Platzhalter-URL. [Espressifs eigenständiger NVS-Generator](https://github.com/espressif/esp-idf-nvs-partition-gen)

(das ist zwar ein etwas umständlicher Weg, um die WLAN-Credentials auf den ESP zu bringen, aber er stellt sicher, dass die Nutzerdaten (SSID, Passwort) separat vom Code bleiben und alle ESP32, egal welches Wifi sie verwenden, das gleiche .bin File erhalten können. Und eine gute Gelegeneheit zu lernen, wie man NVRAM-Settings separat und individuell in einen ESP32 flashen kann, für die Wifi-Credentials in "echten" Applikationen bevorzuge ich einen anderen Weg, aber dieser Weg hier, diese Daten separat in das NVS des ESP32 zu flashen wäre ganz praktisch wenn man z.B. während der erstmaligen Inbetriebnahme jedem ESP32 im Haus oder Wohnmobil eindeutige Nummern oder Namen vergeben und diesen im NVS des ESP32 speichern will ;-)

## 4. Version 2 bauen und signieren

1. Ändere den Inhalt von `version.txt` von `1` auf `2`. Verwende bei jedem weiteren Update eine höhere ganze Zahl ohne führende Nullen.
2. Baue und signiere mit demselben Schlüssel und derselben Release-URL:

   ```powershell
   pio run -t ota-release
   ```

3. Der Befehl prüft die Signatur mit dem öffentlichen Schlüssel. Im Ordner **`.pio/build/esp32dev/ota/`** liegen **`firmware.bin`** und **`build-info.json`** für das Release. Die JSON-Datei enthält die Firmware-Version und den UTC-Zeitstempel des erzeugten App-Images. PlatformIO verwendet für USB-Uploads die Datei **`.pio/build/esp32dev/firmware.bin`**. Das Release-Image enthält zusätzlich den Versionsheader und die Signatur.

## 5. Auf GitHub veröffentlichen und OTA prüfen

1. Committe und pushe den zu Version 2 gehörenden Quellstand.
2. Öffne im öffentlichen Repository **Releases → Draft a new release**. Lege den Tag `v2` für diesen Commit an, gib einen Release-Titel ein und lade **`firmware.bin`** und **`build-info.json`** aus **`.pio/build/esp32dev/ota/`** als Assets hoch.
3. Veröffentliche das Release als **Latest**. Die feste Adresse aus `platformio.ini` führt jetzt zu diesem Asset. [GitHub zu Release-Asset-Links](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases)
4. Lass Version 1 auf dem Board laufen. Nach WLAN-Verbindung und Uhrzeitsynchronisation sollte das Board beim nächsten erfolgreichen Check das Update installieren, neu starten und `hello world 2` ausgeben.

Jede `hello world`-Zeile beginnt mit der Uhrzeit in UTC, etwa `14:32:08 UTC hello world 2`. Bis zur Netzzeitsynchronisation steht dort die Laufzeit seit dem Start, etwa `uptime 5s hello world 2`.

Wenn eine neuere Firmware verfügbar ist, zeigt der Monitor auch Git-Tag, Release-Titel und Build-Datum. Bei gleicher oder älterer Firmware-Version erscheint `OTA: no newer version`. Der ESP32 liest die Metadaten für den Tag, auf den GitHub den Firmware-Download umleitet, und aktualisiert den Cache nach fünf Minuten oder bei einem Versions- oder Tagwechsel. Bei älteren Releases ohne `build-info.json` erscheint für das Build-Datum `unavailable`.

Wenn das Release einen Beschreibungstext enthält, zeigt der Monitor zusätzlich die ersten 255 Zeichen der Release Notes. Der Auszug verwendet das Markdown aus GitHub und erhält Zeilenumbrüche.

Der Monitor zeigt die HTTP-Statuscodes, die gefundene Version, die Signaturprüfung und den Neustart. Vor jeder GET-Anfrage zeigt `GET line` ihre Zeilenlänge inklusive Query-Parametern. `line buffer minimum` zählt das abschließende Nullbyte dazu. Der konfigurierte Puffer braucht darüber hinaus Platz für HTTP-Header und Spielraum für wechselnde GitHub-Weiterleitungen.

Wenn Version 1 weiterläuft, öffne die Release-URL ohne GitHub-Anmeldung und prüfe, ob `firmware.bin` heruntergeladen wird. Prüfe außerdem das WLAN, den Zugang zu `pool.ntp.org` über UDP-Port 123 und ob beide Builds denselben Prüfschlüssel verwenden. Beim Reset können ROM und Bootloader eigene Meldungen ausgeben.

## Module in einem anderen Arduino-Projekt verwenden

- `src/wifi_connection.h` und `src/wifi_connection.cpp` lesen die WLAN-Daten aus `credentials` und verbinden den ESP32. Rufe `WifiConnection::begin()` im Setup und `WifiConnection::reportStatus()` im Loop auf. Für diese WLAN-Einrichtung benötigt das Zielprojekt die NVS-Partition `credentials` und den Namensraum `wifi`.
- `src/ota_update.h` und `src/ota_update.cpp` übernehmen HTTPS, Release-Infos, Signaturprüfung und Installation. Das Zielprojekt braucht zwei OTA-App-Slots und eine `otadata`-Partition. Rufe einmal `OtaUpdate::begin()` auf. Rufe `OtaUpdate::checkAndInstall()` synchron aus `loop()` mit Release-URL, laufender Versionsnummer und öffentlichem P-256-Schlüssel auf, sobald WLAN und Systemzeit verfügbar sind. Die Demo reserviert dafür mit `SET_LOOP_TASK_STACK_SIZE(10240)` einen größeren Stack für die Arduino-Schleife. Bei `true` kann die Anwendung neu starten.
- `main.cpp` zeigt die Anbindung mit Arduino-Setup, 30-Sekunden-Prüfung, Uhrzeitsynchronisation und serieller Ausgabe. Für denselben Release-Build übernimm auch `scripts/ota_release.py`, `version.txt` und die zugehörigen Einstellungen aus `platformio.ini`.
