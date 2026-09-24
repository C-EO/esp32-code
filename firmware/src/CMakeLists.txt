# Registers everything in src/ as the main IDF component and declares
# which built-in ESP-IDF components it depends on. PlatformIO's
# espidf framework picks this up automatically.

idf_component_register(
    SRCS
        "main.c"
        "sensor_task.c"
        "relay_task.c"
        "wifi_task.c"
    INCLUDE_DIRS
        "."
        "../include"
    REQUIRES
        driver
        esp_wifi
        esp_event
        esp_netif
        esp_http_client
        nvs_flash
        json
)
