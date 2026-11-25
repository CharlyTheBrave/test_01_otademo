## What this is about

This is my “learning and development playground repository” for experimenting with over-the-air (OTA) updates. The ultimate goal is to deploy new software versions to my family of ESP32-based projects with a free-of-charge OTA using GitHub as the download server infrastructure. This bare-minimum-solution helped me getting familiar with the whole procedure and is my playground to verify new homegrown OTA procedures before I apply them to my other projects. [Find my other projects on GitHub.](https://github.com/CharlyTheBrave)

It is also my first GitHub repository ever, a great learning endeavour on its own. ;-)

Most of the code was written with ChatGPT/Codex. The architectural notes below explain what I changed in the initial proposal and why. They may be helpful when you design a homegrown OTA solution for your own ESP32 application.

For hobbyist-projects and a small fleet of ESP32s checking and downloading updates in a reasonable interval, the small sizes of the ESP32-firmware (usually 1-2 MB) and infrequent new releases (once a month or even less) one should never hit any of the official or inofficial traffic limits of a free account in GitHub...

Alternatives: Plenty and many of them commercial - e.g. https://simpleota.com/blog/esp32-ota-services-compared/ has a decent overview at the time of writing this. You might also want to look into https://esp-github-ota.readthedocs.io/en/latest/README.html which also uses GitHub releases as download server.

## If you want to re-use the solution in this repository

This OTA solution is most conveniently used with VS Code and the PlatformIO extension. You can fork this repository or download it for use on a local copy to try the whole process before applying it to a real-life ESP32 application.

In any case you need to set up a **public** GitHub repository where you can create a **Release** and attach the signed `firmware.bin`. The ESP32 uses a fixed URL to find the latest release and download its firmware from GitHub. You can include `build-info.json` in the same release if you want the ESP32 to display the build date next to other release details.

The public repository needs a commit for the release tag. A short README is enough to satisfy this need. You can still keep the application source code locally or in a separate private GitHub repository if you prefer not to publish the source code itself as well in the same repository. The public GitHub repository is a simple and cheap way to have your ESP32s freely access the latest .bin file from anywhere on the world - internet access through its Wifi as a precondition.

For the complete build, signing and publishing steps, see the [OTA instructions (English)](docs/ota-instructions.md) or the [OTA guide (German)](docs/ota-anleitung.md). Both explain which modules and partitions to carry over to another project.

## The current solution after tweaking the initial ChatGPT proposal

- **The demo application.** The ESP32 prints its current version once a second on the serial console and checks GitHub for updates every 30 seconds. If it finds a newer version, it downloads the image into the inactive OTA partition, verifies its signature and reboots into the new version. You can then build, sign and publish another release to see the process repeat.

- **Reusable modules.** The code is split into [Wi-Fi connection](src/wifi_connection.h) and [OTA update](src/ota_update.h) modules, each with a corresponding `.cpp` file. This makes it easier to reuse the OTA code in an application that already knows how to establish a Wi-Fi connection.

- **The three OTA steps.** The demo runs them in order without user intervention. Other applications can run the quick check regularly, notify the user, show the release details on request, and install only after the user confirms. That's why the demo already splits the whole procedure into three steps.

  1. `OtaUpdate::quickCheck()` reads [version-latest.txt](version-latest.txt) and returns its version number if it is newer than the installed one.
  2. `OtaUpdate::getReleaseDetails()` reads the first eight bytes of the release asset and checks that the firmware version matches the published number. It then returns the Git tag, release title, build date (if `build-info.json` is attached), and up to 255 characters of release notes. The demo prints these details before installation.
  3. `OtaUpdate::installUpdate()` reopens the selected asset and rechecks the header. It writes the image to the inactive partition, verifies the signature over the header and image, and selects the slot for reboot. It returns the installed version on success. The demo then restarts the ESP32.

- **Wi-Fi credentials are provisioned separately.** This is one of the ideas ChatGPT proposed that I liked. First flash the program. Then use the tools in the OTA guide to write the SSID and password to the ESP32’s NVS partition. It is an extra step, but it lets devices on different networks run the same identical firmware. I typically use other means to have an ESP32 learn about his network (e.g. the ESP32 uses WPS or is mimicing an access point captive portal you can easily access with a mobile phone and allows you to set the wifi credentials the ESP32 should then use to connect as a station). So the Wifi and NVS part I will not use elsewhere. But the sepearate NVS-provisioning  is usable to set an individual hostname or a device ID when first flashing the device. Instead of using the NVS an application might also want to use a separate LittleFS data partition which, like the NVS, survives the OTA update procedure which only writes to the application partitions.

- **The firmware version is set manually.** I increase `version.txt` before building a new release. This version will be added to the header of the .bin file, allowing the ESP to double-check the .bin file is newer and worth installing. The GitHub tag (for example, `v1.0.3`) is for users reading the release page only. Same applies to `version-latest.txt` -  the manual change in the file after a new release was published will trigger the devices to look for the .bin file, check and eventually install it.

### What I dropped from the initial ChatGPT proposal and why

ChatGPT initially put the OTA check into a separate RTOS task which I consider an overkill. That makes sense for a check of the .bin file which can take 10–15 seconds as the ESP32 has to follow several GitHub redirects to find the latest file and finally read the firmware header from it. I moved the check into the Arduino loop, starting with the quick check of `version-latest.txt` which should normally not be a huge blocker (1 second or so). In case the application uses some central scheduler (like the self-written tiny scheduler I prefer to use) then this check can be scheduled for off hours once a day. Or it is manually triggered just-in-time through a user interaction where a short delay for the check is also acceptable, no need for an RTOS task which needs extra care.
Similar applies to devices that would silently auto-update due to a lack of user interfaces: for that type of devices a user would not recognise a short service interruption for update checks and the time-window where an update with reboot would be allowed has to be determined anyways, so the pre-check could simply happen in this "service-window" where downtime would be allowed.

## Future enhancements

- **Test releases over OTA.** I haven’t tried publishing test builds alongside regular releases in the same repository. I usually flash test versions to one device over USB and use OTA once I’m ready to update several devices.
- **Signing-key recovery.** The demo currently has one trusted signing key. A future version could prepare for key compromise by adding key rotation backed by a separately stored recovery key.
- **Build the firmware version from the Git tag.** For releases that follow a convention like `v0.0.3`, the build could derive and sign the numeric version automatically. That would avoid publishing a new GitHub release whose firmware still has the previous version number - however, with a "latest.txt" mentioned above some more sophisticated build procedure might make more sense.
- **Supporting update pre-conditions** e.g. direct update from 3 to 6 not allowed, must first install 5 as there might be breaking changes (e.g. modified OTA installer).
