#include <Arduino.h>
#include <cstring>
#include <ctime>

#include "esp_timer.h"

#include "ota_config.h"
#include "ota_update.h"
#include "wifi_connection.h"

// Give synchronous OTA downloads and signature verification extra stack space in loop().
SET_LOOP_TASK_STACK_SIZE(10240);

namespace {
constexpr uint32_t helloInterval = 1000;
constexpr time_t earliestValidTime = 1704067200; // 2024-01-01 UTC
constexpr uint32_t otaInterval = 30000;
uint32_t nextHello;
uint32_t lastOtaCheck;
bool otaEnabled = false;
} // namespace

// Initialize serial output, start Wi-Fi and time synchronization, and enable OTA
// when the release URL and public key are configured.
void setup() {
    Serial.begin(115200);
    nextHello = millis();
    Serial.printf("OTA: check interval %lu s, HTTP TX buffer %d bytes\n",
                  static_cast<unsigned long>(otaInterval / 1000), OtaUpdate::httpTxBuffer);

    if (!WifiConnection::begin()) {
        return;
    }
    configTime(0, 0, "pool.ntp.org");
    otaEnabled = hasOtaKey && strstr(otaUrl, "DEIN-KONTO/DEIN-REPO") == nullptr &&
                 strstr(otaLatestVersionUrl, "DEIN-KONTO/DEIN-REPO") == nullptr &&
                 OtaUpdate::begin();
    lastOtaCheck = millis();
}

// Run OTA checks every 30 seconds, report Wi-Fi status changes, and print
// a timestamped hello message once per second while the loop is running.
void loop() {

    // the loop of the application needs to call the OTA-check in the interval it likes to do so
    // The activity is intentionally split into 3 steps as in a real-world application a user-interaction may be put inbetween them:
    // 1) Frequently check if we have a new version -> let user know
    // 2) show the new version details -> user accepts or dismisses the update
    // 3) install the update
    // in a fully automated update no interaction may happen, 2 is skipped and 1 & 3 run in a suitable service-hour

    if (otaEnabled && millis() - lastOtaCheck >= otaInterval) {
        lastOtaCheck = millis();
        if (!WifiConnection::connected()) {
            Serial.println("OTA: check skipped, WiFi disconnected");
        } else if (time(nullptr) < earliestValidTime) {
            Serial.println("OTA: check skipped, waiting for clock sync");
        } else {
            // 1. Check the published version without opening the firmware asset.
            Serial.printf("OTA: checking for updates (current version %lu)\n",
                          static_cast<unsigned long>(firmwareVersion));
            const uint32_t publishedVersion = OtaUpdate::quickCheck(otaLatestVersionUrl, firmwareVersion);
            if (publishedVersion) {
                // 2. Verify the asset's version and show the release details.
                Serial.printf("OTA: published version %lu, checking release asset\n",
                              static_cast<unsigned long>(publishedVersion));
                OtaUpdate::ReleaseDetails details;
                if (OtaUpdate::getReleaseDetails(otaUrl, firmwareVersion, publishedVersion, details)) {
                    Serial.printf("OTA: release %lu | Git tag: %s | title: %s | built at: %s\n",
                                  static_cast<unsigned long>(details.version),
                                  details.tag.isEmpty() ? "unavailable" : details.tag.c_str(),
                                  details.title.isEmpty() ? "unavailable" : details.title.c_str(),
                                  details.builtAt.isEmpty() ? "unavailable" : details.builtAt.c_str());
                    if (!details.notes.isEmpty()) {
                        Serial.printf("OTA: release notes (up to 255 characters)\n%s\n", details.notes.c_str());
                    }

                    // 3. Install automatically in this demo, then restart on success.
                    if (OtaUpdate::installUpdate(details, firmwareVersion, otaPublicKey)) {
                        Serial.println("OTA: restarting");
                        ESP.restart();
                    }
                }
            }
        }
    }

    // This will just print a timestamp with the current version for the demo
    const uint32_t now = millis();
    WifiConnection::reportStatus();
    if (static_cast<int32_t>(now - nextHello) >= 0) {
        const time_t wallTime = time(nullptr);
        char timestamp[32];
        struct tm utc{};
        if (wallTime < earliestValidTime || !gmtime_r(&wallTime, &utc) ||
            !strftime(timestamp, sizeof(timestamp), "%H:%M:%S UTC", &utc)) {
            snprintf(timestamp, sizeof(timestamp), "uptime %llus",
                     static_cast<unsigned long long>(esp_timer_get_time() / 1000000));
        }
        Serial.printf("%s hello world %lu\n", timestamp, static_cast<unsigned long>(firmwareVersion));
        nextHello = now + helloInterval;
    }
    delay(1);

    // and here also comes what your loop *really* needs to do besides checking for OTA updates ;-)

}
