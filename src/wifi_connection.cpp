#include "wifi_connection.h"

#include <Arduino.h>
#include <WiFi.h>

#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr uint32_t wifiConnectTimeout = 15000;
uint32_t wifiStartedAt;
bool wifiStarted = false;
bool wifiWasConnected = false;
bool wifiStatusReported = false;

bool loadWifi(char (&ssid)[33], char (&password)[65]) {
    if (nvs_flash_init_partition("credentials") != ESP_OK) {
        return false;
    }
    nvs_handle_t handle;
    if (nvs_open_from_partition("credentials", "wifi", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t ssidSize = sizeof(ssid);
    size_t passwordSize = sizeof(password);
    const bool valid = nvs_get_str(handle, "ssid", ssid, &ssidSize) == ESP_OK &&
                       nvs_get_str(handle, "password", password, &passwordSize) == ESP_OK &&
                       ssid[0] != '\0';
    nvs_close(handle);
    return valid;
}
} // namespace

namespace WifiConnection {

bool begin() {
    char ssid[33];
    char password[65];
    if (!loadWifi(ssid, password)) {
        Serial.println("WiFi not connected: credentials missing or unreadable");
        return false;
    }

    Serial.printf("WiFi connecting: SSID=\"%s\", password=%s\n",
                  ssid, password[0] ? "set" : "empty");
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
    wifiStartedAt = millis();
    wifiStarted = true;
    return true;
}

bool connected() {
    return WiFi.status() == WL_CONNECTED;
}

void reportStatus() {
    const uint32_t now = millis();
    if (wifiStarted) {
        const wl_status_t status = WiFi.status();
        const bool connected = status == WL_CONNECTED;
        if (connected && !wifiWasConnected) {
            Serial.printf("WiFi connected: IP=%s\n", WiFi.localIP().toString().c_str());
            wifiStatusReported = true;
        } else if (!connected && (wifiWasConnected ||
                   (!wifiStatusReported && now - wifiStartedAt >= wifiConnectTimeout))) {
            Serial.printf("WiFi not connected: status=%d\n", static_cast<int>(status));
            wifiStatusReported = true;
        }
        wifiWasConnected = connected;
    }
}

} // namespace WifiConnection
