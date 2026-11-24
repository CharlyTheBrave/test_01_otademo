## What this is about

This is my “learning and development playground repository” for experimenting with over-the-air (OTA) updates. The ultimate goal is to deploy new software versions to my family of ESP32-based projects with OTA and this bare-minimum-solution helped me getting familiar with the whole procedure and is my playground to verify new OTA procedures before I apply them to my other projects. [Find my other projects on GitHub.](https://github.com/CharlyTheBrave)

It is also my first GitHub repository ever, a great learning endeavour on its own. ;-)

Most of the code was written with ChatGPT/Codex. The architectural notes below explain what I changed in the initial proposal and why. They may be helpful when you design an OTA solution for your own ESP32 application.

## If you want to re-use the solution in this repository

This OTA solution is most conveniently used with VS Code and the PlatformIO extension. You can fork this repository or download it for use on a local copy to try the whole process before applying it to a real-life ESP32 application.

In any case you need to set up a **public** GitHub repository where you can create a **Release** and attach the signed `firmware.bin`. The ESP32 uses a fixed URL to find the latest release and download its firmware. You can include `build-info.json` in the same release if you want the ESP32 to display the build date next to other release details.

The public repository needs a commit for the release tag. A short README is enough to satisfy this need. You can still keep the application source code locally or in a separate private GitHub repository if you prefer not to publish the source code itself as well in the same repository. The public GitHub repository is a simple and cheap way to have your ESP32s freely access the latest .bin file from anywhere on the world - internet access through its Wifi as a precondition.

For the complete build, signing and publishing steps, see the [OTA instructions (English)](docs/ota-instructions.md) or the [OTA guide (German)](docs/ota-anleitung.md). Both explain which modules and partitions to carry over to another project.

## The current solution after tweaking the initial ChatGPT proposal

- **The demo application.** The ESP32 prints its current version once a second on the serial console and checks GitHub for updates every 30 seconds. If it finds a newer version, it downloads the image into the inactive OTA partition, verifies its signature and reboots into the new version. You can then build, sign and publish another release to see the process repeat.

- **Reusable modules.** The code is split into [Wi-Fi connection](src/wifi_connection.h) and [OTA update](src/ota_update.h) modules, each with a corresponding `.cpp` file. This makes it easier to reuse the OTA code in an application that already knows how to establish a Wi-Fi connection.

- **The version check.** The ESP32 reads the first eight bytes of the signed release asset (the .bin file you created e.g. locally and uploaded to GitHub during the creation of a new release in GitHub). These bytes contain an identifying marker and the numeric firmware version from [version.txt](version.txt). If the offered version is equal to or older than the installed one, the ESP32 closes the connection after reading the header. For a newer version, it downloads the rest and verifies the signature over the header and firmware before writing to the unused partition and switching to it for the re-boot. In case you forgot to increase the release number in version.txt before building the .bin file the ESPs already on this release will think "nothing new in this .bin file" and will ignore your new .bin file ;-) 

- **Release information.** When a newer version is available, the demo shows the Git tag, release title, build date (if `build-info.json` is attached) and the first 255 characters of the release notes. The demo installs updates automatically. In my own applications with a user interface, I would prefer to show this information and let the user decide whether to install the update.

- **Wi-Fi credentials are provisioned separately.** This is one of the ideas ChatGPT proposed that I liked. First flash the program. Then use the tools in the OTA guide to write the SSID and password to the ESP32’s NVS partition. It is an extra step, but it lets devices on different networks run the same identical firmware. The same approach could be used to provision a hostname or device ID. A real-life application could also store additional settings in a separate LittleFS data partition which, like the NVS, survives an OTA update.

- **The firmware version is set manually.** I increase `version.txt` before building a new release. The GitHub tag (for example, `v1.0.3`) is for users reading the release page. I like keeping the numeric version in the signed header as the value devices use to decide whether to update as it is an additional gate to trigger devices download new releases.

### What I dropped from the initial ChatGPT proposal and why

ChatGPT initially put the OTA check into a separate RTOS task. That makes sense for background updates because the check alone can take 10–15 seconds as the ESP32 has to follow GitHub’s redirects to find the latest .bin file and finally read the firmware header. I moved the check into the Arduino loop. It blocks the rest of the loop while it runs, which suits the way I want to integrate it into other projects:

In those projects, I would make the check part of startup (which would have to wait for the result anyways, no benefit from putting it into a separate task) or let the user initiate it from a display or a web page served by the ESP32 (here the separate task my make sense again to check in the background IF a new version is available). A separate RTOS task is the best solution for a device that checks for updates silently, though the reboot would have to be coordinated or triggered with whatever else the device is doing (like collecting, presenting, forwarding data). I am also considering publishing a simple "Lastest Release" file or JSON on a fixed GitHub location (e.g. https://raw.githubusercontent.com/account/repo/main/latest-prod.txt) which makes the checking easier and faster for any ESP32 looking for updates (also see Tauri Updater, Electron Builder, Mender Device API and similar solutions). version.txt could also be natively used for simple checking but there will be a delay between committing the source code and publishing the .bin file - so a client ESP might in theory announce a new release to a user only to later not find the newer .bin file because it's not yet there.

## Future enhancements

- **Test releases over OTA.** I haven’t tried publishing test builds alongside regular releases in the same repository. I usually flash test versions to one device over USB and use OTA once I’m ready to update several devices.
- **Signing-key recovery.** The demo currently has one trusted signing key. A future version could prepare for key compromise by adding key rotation backed by a separately stored recovery key.
- **Build the firmware version from the Git tag.** For releases that follow a convention like `v0.0.3`, the build could derive and sign the numeric version automatically. That would avoid publishing a new GitHub release whose firmware still has the previous version number - however, with a "latest.txt" mentioned above some more sophisticated build procedure might make more sense.
