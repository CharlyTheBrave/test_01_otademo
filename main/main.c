#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if !CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT || !CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME
#error "The OTA demo requires ECDSA signature verification on update"
#endif

static const char *TAG = "ota_demo";
static EventGroupHandle_t wifi_events;
static const EventBits_t WIFI_HAS_IP = BIT0;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_HAS_IP);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_HAS_IP);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
}

static esp_err_t load_wifi_config(wifi_config_t *config)
{
    esp_err_t err = nvs_flash_init_partition("credentials");
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t nvs;
    err = nvs_open_from_partition("credentials", "wifi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    // NVS strings include a NUL, even when Wi-Fi's fields are completely filled.
    char ssid[sizeof(config->sta.ssid) + 1];
    char password[sizeof(config->sta.password) + 1];
    size_t ssid_size = sizeof(ssid);
    size_t password_size = sizeof(password);
    err = nvs_get_str(nvs, "ssid", ssid, &ssid_size);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, "password", password, &password_size);
    }
    if (err == ESP_OK) {
        memcpy(config->sta.ssid, ssid, ssid_size - 1);
        memcpy(config->sta.password, password, password_size - 1);
    }
    nvs_close(nvs);
    return err;
}

static void start_wifi(wifi_config_t *config)
{
    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_events ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static uint32_t version_number(const char *version, size_t capacity)
{
    uint32_t value = 0;
    for (size_t i = 0; i < capacity; ++i) {
        char digit = version[i];
        if (digit == '\0') {
            return value;
        }
        if (digit < '0' || digit > '9' || (i == 0 && digit == '0') ||
            value > (UINT32_MAX - (digit - '0')) / 10) {
            return 0;
        }
        value = value * 10 + (digit - '0');
    }
    return 0;
}

static esp_err_t check_for_update(void)
{
    const esp_http_client_config_t http = {
        .url = CONFIG_OTA_DEMO_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };
    const esp_https_ota_config_t ota = { .http_config = &http };
    esp_https_ota_handle_t update;
    esp_err_t err = esp_https_ota_begin(&ota, &update);
    if (err != ESP_OK) {
        return err;
    }

    esp_app_desc_t candidate;
    err = esp_https_ota_get_img_desc(update, &candidate);
    if (err == ESP_OK) {
        const esp_app_desc_t *running = esp_app_get_description();
        uint32_t current = version_number(running->version, sizeof(running->version));
        uint32_t available = version_number(candidate.version, sizeof(candidate.version));
        if (strncmp(candidate.project_name, running->project_name,
                    sizeof(candidate.project_name)) != 0 || current == 0 || available == 0) {
            err = ESP_ERR_INVALID_VERSION;
        } else if (available > current) {
            ESP_LOGI(TAG, "Installing version %" PRIu32, available);
            do {
                err = esp_https_ota_perform(update);
            } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);
            if (err == ESP_OK && esp_https_ota_is_complete_data_received(update)) {
                // finish verifies the image signature before selecting the boot partition.
                err = esp_https_ota_finish(update);
                if (err == ESP_OK) {
                    esp_restart();
                }
                return err;
            }
            if (err == ESP_OK) {
                err = ESP_ERR_INVALID_SIZE;
            }
        }
    }
    esp_https_ota_abort(update);
    return err;
}

static void ota_task(void *arg)
{
    bool sntp_started = false;
    bool time_synced = false;
    TickType_t last_check = xTaskGetTickCount();
    while (true) {
        if (xEventGroupGetBits(wifi_events) & WIFI_HAS_IP) {
            if (!sntp_started) {
                esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
                sntp_started = esp_netif_sntp_init(&sntp) == ESP_OK;
            }
            if (sntp_started && !time_synced) {
                time_synced = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)) == ESP_OK;
            }
            if (time_synced) {
                esp_err_t err = check_for_update();
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "OTA check failed: %s", esp_err_to_name(err));
                }
            }
        }
        xTaskDelayUntil(&last_check, pdMS_TO_TICKS(60000));
    }
}

void app_main(void)
{
    const char *version = esp_app_get_description()->version;
    const TickType_t hello_interval = pdMS_TO_TICKS(1000);
    TickType_t last_hello = xTaskGetTickCount();
    printf("hello world %s\n", version);

    wifi_config_t config = { 0 };
    if (strncmp(CONFIG_OTA_DEMO_URL, "https://", 8) == 0 &&
        strstr(CONFIG_OTA_DEMO_URL, "DEIN-KONTO/DEIN-REPO") == NULL &&
        load_wifi_config(&config) == ESP_OK && config.sta.ssid[0] != '\0') {
        start_wifi(&config);
        ESP_ERROR_CHECK(xTaskCreate(ota_task, "ota", 8192, NULL, 4, NULL) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);
    }

    while (true) {
        xTaskDelayUntil(&last_hello, hello_interval);
        printf("hello world %s\n", version);
    }
}
