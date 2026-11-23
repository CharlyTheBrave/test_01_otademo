#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <ctime>
#include <memory>
#include <strings.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

#include "ota_config.h"

namespace {
constexpr size_t headerSize = 8;
constexpr size_t signatureSize = 68; // espsecure V1: four zero bytes, then P-256 r || s
constexpr uint32_t helloInterval = 1000;
constexpr time_t earliestValidTime = 1704067200; // 2024-01-01 UTC
constexpr uint32_t wifiConnectTimeout = 15000;
constexpr uint32_t otaInterval = 30000;
constexpr int otaHttpTxBuffer = 4096;
constexpr uint32_t releaseInfoInterval = 300000;
uint32_t nextHello;
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

size_t getRequestLineLength(const char *url) {
    const char *schemeEnd = strstr(url, "://");
    if (!schemeEnd) {
        return 0;
    }
    const char *target = strpbrk(schemeEnd + 3, "/?#");
    size_t targetLength = target ? strcspn(target, "#") : 0;
    if (targetLength && target[targetLength - 1] == '?' && strchr(target, '?') == target + targetLength - 1) {
        --targetLength;
    }
    const size_t defaultPathLength = target && *target == '/' ? 0 : 1;
    return sizeof("GET  HTTP/1.1\r\n") - 1 + targetLength + defaultPathLength;
}

struct HttpTrace {
    size_t requestLineLength = 0;
    String releaseAssetUrl;
};

esp_err_t recordRedirect(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        event->header_value && strcasecmp(event->header_key, "Location") == 0) {
        auto &trace = *static_cast<HttpTrace *>(event->user_data);
        trace.requestLineLength = getRequestLineLength(event->header_value);
        constexpr char github[] = "https://github.com/";
        if (strncmp(event->header_value, github, sizeof(github) - 1) == 0 &&
            strstr(event->header_value, "/releases/download/")) {
            trace.releaseAssetUrl = event->header_value;
        }
    }
    return ESP_OK;
}

int64_t openRelease(esp_http_client_handle_t http, HttpTrace &trace, const char *label = "OTA") {
    for (int redirects = 0; redirects <= 5; ++redirects) {
        if (trace.requestLineLength > 0) {
            Serial.printf("%s: HTTP request %d GET line: %u bytes (line buffer minimum %u, configured %d)\n",
                          label, redirects + 1, static_cast<unsigned>(trace.requestLineLength),
                          static_cast<unsigned>(trace.requestLineLength + 1), otaHttpTxBuffer);
        }
        const esp_err_t openError = esp_http_client_open(http, 0);
        if (openError != ESP_OK) {
            Serial.printf("%s: HTTP request %d failed: %s (%d)\n",
                          label, redirects + 1, esp_err_to_name(openError), static_cast<int>(openError));
            return -1;
        }
        trace.requestLineLength = 0; // The response's Location header supplies the next GET length.
        const int64_t size = esp_http_client_fetch_headers(http);
        const int status = esp_http_client_get_status_code(http);
        if (size < 0) {
            Serial.printf("%s: HTTP response headers failed (%lld)\n", label, static_cast<long long>(size));
            return -1;
        }
        Serial.printf("%s: HTTP request %d returned %d\n", label, redirects + 1, status);
        if (status == 200) {
            return size;
        }
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
            return -1;
        }
        if (redirects == 5 || esp_http_client_set_redirection(http) != ESP_OK) {
            Serial.println("OTA: redirect rejected or limit reached");
            return -1;
        }
        char url[2048];
        if (esp_http_client_get_url(http, url, sizeof(url)) != ESP_OK ||
            strncmp(url, "https://", 8) != 0) {
            Serial.println("OTA: redirect requires HTTPS");
            return -1;
        }
        esp_http_client_clear_response_buffer(http);
        esp_http_client_close(http);
    }
    return -1;
}

esp_http_client_handle_t createHttpClient(const char *url, HttpTrace &trace) {
    trace.requestLineLength = getRequestLineLength(url);
    esp_http_client_config_t config{};
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 10000;
    // GitHub's asset redirect adds a signed query to the GET request line.
    config.buffer_size_tx = otaHttpTxBuffer;
    config.disable_auto_redirect = true;
    config.event_handler = recordRedirect;
    config.user_data = &trace;
    return esp_http_client_init(&config);
}

cJSON *fetchJson(const char *url, size_t limit) {
    std::unique_ptr<char[]> body(new (std::nothrow) char[limit + 1]);
    HttpTrace trace;
    auto http = body ? createHttpClient(url, trace) : nullptr;
    if (!http) {
        return nullptr;
    }
    const int64_t size = openRelease(http, trace, "Release info");
    const int count = size >= 0 && size <= static_cast<int64_t>(limit)
                          ? esp_http_client_read_response(http, body.get(), size > 0 ? size : limit + 1)
                          : -1;
    const bool complete = count > 0 && count <= static_cast<int>(limit) &&
                          (size == 0 || count == size) &&
                          esp_http_client_is_complete_data_received(http);
    esp_http_client_cleanup(http);
    if (!complete) {
        return nullptr;
    }
    body[count] = '\0';
    return cJSON_ParseWithLengthOpts(body.get(), count + 1, nullptr, true);
}

struct ReleaseInfo {
    String assetUrl;
    String tag;
    String title;
    String builtAt;
    String notes;
    uint32_t version = 0;
    uint32_t checkedAt = 0;
};

void copyReleaseNotes(String &preview, const char *notes) {
    if (!notes) {
        return;
    }
    constexpr size_t maxCharacters = 255;
    const char *end = notes;
    for (size_t characters = 0; *end && characters < maxCharacters; ++characters) {
        ++end;
        // Keep the continuation bytes of the current UTF-8 character.
        while ((static_cast<unsigned char>(*end) & 0xC0) == 0x80) {
            ++end;
        }
    }
    preview.concat(notes, end - notes);
}

void showReleaseInfo(ReleaseInfo &info, const String &assetUrl, uint32_t version) {
    constexpr char github[] = "https://github.com/";
    constexpr char download[] = "/releases/download/";
    constexpr char firmwareAsset[] = "/firmware.bin";
    const int repoEnd = assetUrl.indexOf(download, sizeof(github) - 1);
    if (repoEnd < 0 || !assetUrl.endsWith(firmwareAsset)) {
        Serial.println("OTA: GitHub release metadata unavailable for this asset URL");
        return;
    }
    if (info.assetUrl != assetUrl || info.version != version ||
        millis() - info.checkedAt >= releaseInfoInterval) {
        info = {};
        info.assetUrl = assetUrl;
        info.version = version;
        info.checkedAt = millis();
        const auto tagStart = repoEnd + sizeof(download) - 1;
        const auto tagEnd = assetUrl.length() - (sizeof(firmwareAsset) - 1);
        const String apiUrl = String("https://api.github.com/repos/") +
                              assetUrl.substring(sizeof(github) - 1, repoEnd) +
                              "/releases/tags/" + assetUrl.substring(tagStart, tagEnd);
        cJSON *release = fetchJson(apiUrl.c_str(), 16384);
        const char *tag = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(release, "tag_name"));
        const char *title = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(release, "name"));
        if (tag) {
            info.tag = tag;
        }
        if (title) {
            info.title = title;
        }
        copyReleaseNotes(info.notes, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(release, "body")));
        const cJSON *assets = cJSON_GetObjectItemCaseSensitive(release, "assets");
        const cJSON *asset;
        cJSON_ArrayForEach(asset, assets) {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(asset, "name"));
            if (name && strcmp(name, "build-info.json") == 0) {
                const String buildUrl = assetUrl.substring(0, tagEnd) + "/build-info.json";
                cJSON *buildInfo = fetchJson(buildUrl.c_str(), 512);
                const auto *buildVersion = cJSON_GetObjectItemCaseSensitive(buildInfo, "firmware_version");
                const char *builtAt = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(buildInfo, "built_at"));
                if (cJSON_IsNumber(buildVersion) && buildVersion->valuedouble == version && builtAt) {
                    info.builtAt = builtAt;
                }
                cJSON_Delete(buildInfo);
                break;
            }
        }
        cJSON_Delete(release);
    }
    Serial.printf("OTA: release %lu | Git tag: %s | title: %s | built at: %s\n",
                  static_cast<unsigned long>(version), info.tag.isEmpty() ? "unavailable" : info.tag.c_str(),
                  info.title.isEmpty() ? "unavailable" : info.title.c_str(),
                  info.builtAt.isEmpty() ? "unavailable" : info.builtAt.c_str());
    if (!info.notes.isEmpty()) {
        Serial.printf("OTA: release notes (up to 255 characters)\n%s\n", info.notes.c_str());
    }
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
         digestSize == sizeof(digest);
    psa_hash_abort(&hash);
    if (!ok) {
        Serial.println("OTA: image transfer, flash write or hashing failed");
        return false;
    }
    const bool valid = verifySignature(digest, signature);
    Serial.println(valid ? "OTA: signature verified" : "OTA: signature rejected");
    return valid;
}

bool installUpdate(esp_http_client_handle_t http, HttpTrace &trace, uint32_t &available) {
    const int64_t size = openRelease(http, trace);
    if (size < 0) {
        return false;
    }
    const auto *partition = esp_ota_get_next_update_partition(nullptr);
    constexpr int64_t overhead = headerSize + signatureSize;
    if (!partition || size <= overhead || size - overhead > partition->size) {
        Serial.printf("OTA: invalid asset length or unavailable OTA slot (%lld bytes)\n",
                      static_cast<long long>(size));
        return false;
    }
    uint8_t header[headerSize];
    if (!readExact(http, header, sizeof(header)) || memcmp(header, "OTA1", 4) != 0) {
        Serial.println("OTA: invalid or incomplete release header");
        return false;
    }
    available = uint32_t(header[4]) | (uint32_t(header[5]) << 8) |
                (uint32_t(header[6]) << 16) | (uint32_t(header[7]) << 24);
    if (available <= firmwareVersion) {
        Serial.printf("OTA: no newer version (release %lu)\n", static_cast<unsigned long>(available));
        return false;
    }

    const size_t imageSize = size - overhead;
    Serial.printf("OTA: downloading version %lu (%u bytes) to %s\n",
                  static_cast<unsigned long>(available), static_cast<unsigned>(imageSize), partition->label);
    esp_ota_handle_t update;
    esp_err_t error = esp_ota_begin(partition, imageSize, &update);
    if (error != ESP_OK) {
        Serial.printf("OTA: opening flash slot failed: %s\n", esp_err_to_name(error));
        return false;
    }
    if (!writeVerifiedImage(http, update, header, imageSize)) {
        esp_ota_abort(update);
        return false;
    }
    error = esp_ota_end(update);
    if (error != ESP_OK) {
        Serial.printf("OTA: image validation failed: %s\n", esp_err_to_name(error));
        return false;
    }
    error = esp_ota_set_boot_partition(partition);
    if (error != ESP_OK) {
        Serial.printf("OTA: selecting boot slot failed: %s\n", esp_err_to_name(error));
        return false;
    }
    Serial.printf("OTA: version %lu installed\n", static_cast<unsigned long>(available));
    return true;
}

void otaTask(void *) {
    TickType_t lastCheck = xTaskGetTickCount();
    ReleaseInfo releaseInfo;
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("OTA: check skipped, WiFi disconnected");
        } else if (time(nullptr) < earliestValidTime) {
            Serial.println("OTA: check skipped, waiting for clock sync");
        } else {
            Serial.printf("OTA: checking for updates (current version %lu)\n",
                          static_cast<unsigned long>(firmwareVersion));
            HttpTrace trace;
            uint32_t available = 0;
            auto http = createHttpClient(otaUrl, trace);
            if (http) {
                const bool installed = installUpdate(http, trace, available);
                esp_http_client_cleanup(http);
                if (available) {
                    showReleaseInfo(releaseInfo, trace.releaseAssetUrl, available);
                }
                if (installed) {
                    Serial.println("OTA: restarting");
                    ESP.restart();
                }
            } else {
                Serial.println("OTA: HTTP client initialization failed");
            }
        }
        xTaskDelayUntil(&lastCheck, pdMS_TO_TICKS(otaInterval));
    }
}
}

void setup() {
    Serial.begin(115200);
    nextHello = millis();
    Serial.printf("OTA: check interval %lu s, HTTP TX buffer %d bytes\n",
                  static_cast<unsigned long>(otaInterval / 1000), otaHttpTxBuffer);

    char ssid[33];
    char password[65];
    if (!loadWifi(ssid, password)) {
        Serial.println("WiFi not connected: credentials missing or unreadable");
        return;
    }

    Serial.printf("WiFi connecting: SSID=\"%s\", password=%s\n",
                  ssid, password[0] ? "set" : "empty");
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
    wifiStartedAt = millis();
    wifiStarted = true;
    configTime(0, 0, "pool.ntp.org");

    if (hasOtaKey && strstr(otaUrl, "DEIN-KONTO/DEIN-REPO") == nullptr &&
        psa_crypto_init() == PSA_SUCCESS) {
        xTaskCreate(otaTask, "ota", 10240, nullptr, 1, nullptr);
    }
}

void loop() {
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
        nextHello += helloInterval;
    }
    delay(1);
}
