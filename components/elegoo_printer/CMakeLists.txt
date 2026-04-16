idf_component_register(
    SRCS "elegoo_printer.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client mqtt cjson freertos
)
