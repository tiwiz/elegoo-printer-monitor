idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_lcd gc9a01_driver elegoo_printer esp_netif esp_event web_config esp_http_server nvs_flash
)
