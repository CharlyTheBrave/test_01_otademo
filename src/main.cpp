#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <ctime>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

#include "ota_config.h"

namespace {
constexpr size_t headerSize = 8;
constexpr size_t signatureSize = 68; // espsecure V1: four zero bytes, then P-256 r || s
constexpr uint32_t helloInterval = 1000;
uint32_t nextHello;

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

int64_t openRelease(esp_http_client_handle_t http) {
    for (int redirects = 0; redirects <= 5; ++redirects) {
        if (esp_http_client_open(http, 0) != ESP_OK) {
            return -1;
        }
        const int64_t size = esp_http_client_fetch_headers(http);
        const int status = esp_http_client_get_status_code(http);
        if (status == 200) {
            return size;
        }
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
            return -1;
        }
        if (esp_http_client_set_redirection(http) != ESP_OK) {
            return -1;
        }
        char url[2048];
        if (esp_http_client_get_url(http, url, sizeof(url)) != ESP_OK ||
            strncmp(url, "https://", 8) != 0) {
            return -1;
        }
        esp_http_client_clear_response_buffer(http);
        esp_http_client_close(http);
    }
    return -1;
}

bool readExact(esp_http_client_handle_t http, uint8_t *bytes, size_t size) {
    return esp_http_client_read_response(http, reinterpret_cast<char *>(bytes), size) ==
           static_cast<int>(size);
}

bool verifySignature(const uint8_t (&digest)[32], const uint8_t (&signature)[signatureSize]) {
    constexpr uint8_t signatureVersion[4] = {};
    if (memcmp(signature, signatureVersion, sizeof(signatureVersion)) != 0) {
        return false;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    const auto algorithm = PSA_ALG_ECDSA(PSA_ALG_SHA_256);
    psa_set_key_algorithm(&attributes, algorithm);
    mbedtls_svc_key_id_t key;
    if (psa_import_key(&attributes, otaPublicKey, sizeof(otaPublicKey), &key) != PSA_SUCCESS) {
        return false;
    }
    const bool valid = psa_verify_hash(key, algorithm, digest, sizeof(digest),
                                      signature + sizeof(signatureVersion), 64) == PSA_SUCCESS;
    psa_destroy_key(key);
    return valid;
}

bool writeVerifiedImage(esp_http_client_handle_t http, esp_ota_handle_t update,
                        const uint8_t (&header)[headerSize], size_t remaining) {
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    bool ok = psa_hash_setup(&hash, PSA_ALG_SHA_256) == PSA_SUCCESS &&
              psa_hash_update(&hash, header, sizeof(header)) == PSA_SUCCESS;
    uint8_t buffer[2048];
    while (ok && remaining > 0) {
        const size_t count = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        ok = readExact(http, buffer, count) &&
             psa_hash_update(&hash, buffer, count) == PSA_SUCCESS &&
             esp_ota_write(update, buffer, count) == ESP_OK;
        remaining -= count;
    }

    uint8_t digest[32];
    uint8_t signature[signatureSize];
    size_t digestSize = 0;
    ok = ok && readExact(http, signature, sizeof(signature)) &&
         esp_http_client_is_complete_data_received(http) &&
         psa_hash_finish(&hash, digest, sizeof(digest), &digestSize) == PSA_SUCCESS &&
         digestSize == sizeof(digest) && verifySignature(digest, signature);
    psa_hash_abort(&hash);
    return ok;
}

bool installUpdate(esp_http_client_handle_t http) {
    const int64_t size = openRelease(http);
    const auto *partition = esp_ota_get_next_update_partition(nullptr);
    constexpr int64_t overhead = headerSize + signatureSize;
    if (!partition || size <= overhead || size - overhead > partition->size) {
        return false;
    }
    uint8_t header[headerSize];
    if (!readExact(http, header, sizeof(header)) || memcmp(header, "OTA1", 4) != 0) {
        return false;
    }
    const uint32_t available = uint32_t(header[4]) | (uint32_t(header[5]) << 8) |
                               (uint32_t(header[6]) << 16) | (uint32_t(header[7]) << 24);
    if (available <= firmwareVersion) {
        return false;
    }

    const size_t imageSize = size - overhead;
    esp_ota_handle_t update;
    if (esp_ota_begin(partition, imageSize, &update) != ESP_OK) {
        return false;
    }
    if (!writeVerifiedImage(http, update, header, imageSize)) {
        esp_ota_abort(update);
        return false;
    }
    return esp_ota_end(update) == ESP_OK && esp_ota_set_boot_partition(partition) == ESP_OK;
}

void otaTask(void *) {
    TickType_t lastCheck = xTaskGetTickCount();
    for (;;) {
        // HTTPS certificate validation needs a reasonably current system clock.
        if (WiFi.status() == WL_CONNECTED && time(nullptr) >= 1704067200) {
            esp_http_client_config_t config{};
            config.url = otaUrl;
            config.crt_bundle_attach = esp_crt_bundle_attach;
            config.timeout_ms = 10000;
            config.disable_auto_redirect = true;
            auto http = esp_http_client_init(&config);
            if (http) {
                const bool installed = installUpdate(http);
                esp_http_client_cleanup(http);
                if (installed) {
                    ESP.restart();
                }
            }
        }
        xTaskDelayUntil(&lastCheck, pdMS_TO_TICKS(60000));
    }
}
}

void setup() {
    Serial.begin(115200);
    nextHello = millis();

    char ssid[33];
    char password[65];
    if (hasOtaKey && strstr(otaUrl, "DEIN-KONTO/DEIN-REPO") == nullptr &&
        loadWifi(ssid, password) && psa_crypto_init() == PSA_SUCCESS) {
        WiFi.persistent(false);
        WiFi.setAutoReconnect(true);
        WiFi.begin(ssid, password);
        configTime(0, 0, "pool.ntp.org");
        xTaskCreate(otaTask, "ota", 10240, nullptr, 1, nullptr);
    }
}

void loop() {
    if (static_cast<int32_t>(millis() - nextHello) >= 0) {
        Serial.printf("hello world %lu\n", static_cast<unsigned long>(firmwareVersion));
        nextHello += helloInterval;
    }
    delay(1);
}
