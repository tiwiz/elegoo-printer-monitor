#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_CONFIG_AP_SSID "ElegooMonitor"

#define WEB_CONFIG_AP_CHANNEL 6
#define WEB_CONFIG_SERVER_PORT 80

#define NVS_NAMESPACE "elegoo_cfg"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"
#define NVS_KEY_PRINTER_HOST "printer_host"
#define NVS_KEY_PRINTER_PORT "printer_port"
#define NVS_KEY_PRINTER_TYPE "printer_type"
#define NVS_KEY_CONFIGURED "configured"

typedef enum {
    PRINTER_TYPE_NEPTUNE4,
    PRINTER_TYPE_NEPTUNE4PRO,
    PRINTER_TYPE_NEPTUNE4MAX,
    PRINTER_TYPE_CENTAURI_CARBON,
    PRINTER_TYPE_CENTAURI_CARBON2,
    PRINTER_TYPE_ORANGESTORM_GIGA,
    PRINTER_TYPE_GENERIC,
    PRINTER_TYPE_COUNT
} printer_type_t;

typedef struct {
    char wifi_ssid[32];
    char wifi_password[64];
    char printer_host[64];
    int printer_port;
    printer_type_t printer_type;
    bool configured;
} web_config_data_t;

typedef void (*web_config_complete_callback_t)(const web_config_data_t *config, void *user_data);

typedef struct web_config_ctx web_config_ctx_t;

esp_err_t web_config_init(web_config_ctx_t **out_ctx);

esp_err_t web_config_start_ap(web_config_ctx_t *ctx);

esp_err_t web_config_stop(web_config_ctx_t *ctx);

esp_err_t web_config_get_saved(web_config_data_t *out_data);

esp_err_t web_config_save(const web_config_data_t *data);

esp_err_t web_config_delete(void);

esp_err_t web_config_is_configured(bool *configured);

esp_err_t web_config_delete_ctx(web_config_ctx_t *ctx);

const char* web_config_printer_type_to_string(printer_type_t type);

printer_type_t web_config_string_to_printer_type(const char *str);

#ifdef __cplusplus
}
#endif
