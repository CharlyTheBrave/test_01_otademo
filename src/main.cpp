#include <Arduino.h>
#include <cstring>
#include <ctime>

#include "esp_timer.h"

#include "ota_config.h"
#include "ota_update.h"
#include "wifi_connection.h"

SET_LOOP_TASK_STACK_SIZE(10240);

namespace {
constexpr uint32_t helloInterval = 1000;
constexpr time_t earliestValidTime = 1704067200; // 2024-01-01 UTC
constexpr uint32_t otaInterval = 30000;
uint32_t nextHello;
uint32_t lastOtaCheck;
bool otaEnabled = false;

void checkForUpdate() {
    if (!WifiConnection::connected()) {
        Serial.println("OTA: check skipped, WiFi disconnected");
    } else if (time(nullptr) < earliestValidTime) {
        Serial.println("OTA: check skipped, waiting for clock sync");
    } else {
        Serial.printf("OTA: checking for updates (current version %lu)\n",
                      static_cast<unsigned long>(firmwareVersion));
        if (OtaUpdate::checkAndInstall(otaUrl, firmwareVersion, otaPublicKey)) {
            Serial.println("OTA: restarting");
            ESP.restart();
        }
    }
}
} // namespace

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
                 OtaUpdate::begin();
    lastOtaCheck = millis();
}

void loop() {
    if (otaEnabled && millis() - lastOtaCheck >= otaInterval) {
        lastOtaCheck = millis();
        checkForUpdate();
    }
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
}
