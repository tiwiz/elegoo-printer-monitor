#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ELEGOO_PRINTER_DEFAULT_PORT 8888
#define ELEGOO_PRINTER_TIMEOUT_MS 5000

typedef enum {
    ELEGOO_PRINTER_TYPE_NEPTUNE4,
    ELEGOO_PRINTER_TYPE_NEPTUNE4PRO,
    ELEGOO_PRINTER_TYPE_NEPTUNE4MAX,
    ELEGOO_PRINTER_TYPE_CENTAURI_CARBON,
    ELEGOO_PRINTER_TYPE_CENTAURI_CARBON2,
    ELEGOO_PRINTER_TYPE_ORANGESTORM_GIGA,
    ELEGOO_PRINTER_TYPE_GENERIC
} elegoo_printer_type_t;

typedef enum {
    ELEGOO_STATE_OFFLINE,
    ELEGOO_STATE_IDLE,
    ELEGOO_STATE_PRINTING,
    ELEGOO_STATE_SELF_CHECKING,
    ELEGOO_STATE_AUTO_LEVELING,
    ELEGOO_STATE_PID_CALIBRATING,
    ELEGOO_STATE_RESONANCE_TESTING,
    ELEGOO_STATE_UPDATING,
    ELEGOO_STATE_FILE_COPYING,
    ELEGOO_STATE_FILE_TRANSFERRING,
    ELEGOO_STATE_HOMING,
    ELEGOO_STATE_PREHEATING,
    ELEGOO_STATE_FILAMENT_OPERATING,
    ELEGOO_STATE_EXTRUDER_OPERATING,
    ELEGOO_STATE_EXCEPTION,
    ELEGOO_STATE_UNKNOWN
} elegoo_printer_state_t;

typedef enum {
    ELEGOO_SUBSTATE_NONE,
    ELEGOO_SUBSTATE_P_HOMING,
    ELEGOO_SUBSTATE_P_AUTO_LEVELING,
    ELEGOO_SUBSTATE_P_PRINTING,
    ELEGOO_SUBSTATE_P_PAUSING,
    ELEGOO_SUBSTATE_P_PAUSED,
    ELEGOO_SUBSTATE_P_STOPPING,
    ELEGOO_SUBSTATE_P_STOPPED,
    ELEGOO_SUBSTATE_P_PRINTING_COMPLETED,
    ELEGOO_SUBSTATE_UNKNOWN
} elegoo_printer_substate_t;

typedef struct {
    float bed_actual;
    float bed_target;
    float nozzle_actual;
    float nozzle_target;
} elegoo_temperature_t;

typedef struct {
    char file_name[128];
    int progress;
    int current_layer;
    int total_layer;
    int current_time;
    int total_time;
    int estimated_time;
} elegoo_print_status_t;

typedef struct {
    elegoo_printer_state_t state;
    elegoo_printer_substate_t substate;
    bool support_progress;
    int progress;
    char exception_codes[256];
} elegoo_status_t;

typedef struct {
    char printer_id[64];
    char name[64];
    char model[64];
    char brand[32];
    char host[32];
    int port;
    char firmware_version[32];
    char serial_number[64];
} elegoo_printer_info_t;

typedef struct {
    elegoo_printer_info_t info;
    elegoo_status_t status;
    elegoo_temperature_t temperature;
    elegoo_print_status_t print_status;
} elegoo_printer_status_t;

typedef void (*elegoo_status_callback_t)(const elegoo_printer_status_t *status, void *user_data);
typedef void (*elegoo_connected_callback_t)(bool connected, void *user_data);

typedef struct {
    char host[64];
    int port;
    elegoo_printer_type_t printer_type;
    char printer_name[64];
    char printer_model[64];
    char printer_brand[32];
    bool auto_reconnect;
    int connection_timeout_ms;
} elegoo_config_t;

typedef void* elegoo_handle_t;

esp_err_t elegoo_init(elegoo_handle_t *out_handle);

esp_err_t elegoo_configure(elegoo_handle_t handle, const elegoo_config_t *config);

esp_err_t elegoo_connect(elegoo_handle_t handle);

esp_err_t elegoo_disconnect(elegoo_handle_t handle);

bool elegoo_is_connected(elegoo_handle_t handle);

esp_err_t elegoo_get_status(elegoo_handle_t handle, elegoo_printer_status_t *status);

esp_err_t elegoo_refresh_status(elegoo_handle_t handle);

esp_err_t elegoo_register_status_callback(elegoo_handle_t handle, elegoo_status_callback_t callback, void *user_data);

esp_err_t elegoo_register_connection_callback(elegoo_handle_t handle, elegoo_connected_callback_t callback, void *user_data);

esp_err_t elegoo_delete(elegoo_handle_t handle);

const char* elegoo_state_to_string(elegoo_printer_state_t state);

const char* elegoo_substate_to_string(elegoo_printer_substate_t substate);

#ifdef __cplusplus
}
#endif
