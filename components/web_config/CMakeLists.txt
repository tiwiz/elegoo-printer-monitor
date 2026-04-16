idf_component_register(
    SRCS "web_config.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server nvs_flash wifi esp_netif cjson freertos
)
