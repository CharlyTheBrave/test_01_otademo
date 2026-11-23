idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES app_update esp_app_format esp_event esp_http_client
                  esp_https_ota esp_netif esp_wifi mbedtls nvs_flash
)
