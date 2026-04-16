#include "elegoo_printer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "elegoo_printer";

#define WEBSOCKET_PATH "/api/v1/websocket"

typedef struct {
    elegoo_config_t config;
    bool connected;
    bool connecting;
    esp_mqtt_client_handle_t mqtt_client;
    char printer_id[64];
    elegoo_status_callback_t status_callback;
    void *status_user_data;
    elegoo_connected_callback_t connected_callback;
    void *connected_user_data;
    elegoo_printer_status_t last_status;
    TaskHandle_t poll_task_handle;
    SemaphoreHandle_t mutex;
} elegoo_impl_t;

static const char* state_strings[] = {
    "OFFLINE", "IDLE", "PRINTING", "SELF_CHECKING", "AUTO_LEVELING",
    "PID_CALIBRATING", "RESONANCE_TESTING", "UPDATING", "FILE_COPYING",
    "FILE_TRANSFERRING", "HOMING", "PREHEATING", "FILAMENT_OPERATING",
    "EXTRUDER_OPERATING", "EXCEPTION", "UNKNOWN"
};

static const char* substate_strings[] = {
    "NONE", "P_HOMING", "P_AUTO_LEVELING", "P_PRINTING", "P_PAUSING",
    "P_PAUSED", "P_STOPPING", "P_STOPPED", "P_PRINTING_COMPLETED", "UNKNOWN"
};

const char* elegoo_state_to_string(elegoo_printer_state_t state) {
    if (state >= 0 && state < sizeof(state_strings) / sizeof(state_strings[0])) {
        return state_strings[state];
    }
    return "UNKNOWN";
}

const char* elegoo_substate_to_string(elegoo_printer_substate_t substate) {
    if (substate >= 0 && substate < sizeof(substate_strings) / sizeof(substate_strings[0])) {
        return substate_strings[substate];
    }
    return "UNKNOWN";
}

static int parse_printer_state(const char *state_str) {
    if (strcmp(state_str, "idle") == 0) return ELEGOO_STATE_IDLE;
    if (strcmp(state_str, "printing") == 0) return ELEGOO_STATE_PRINTING;
    if (strcmp(state_str, "printing") == 0) return ELEGOO_STATE_PRINTING;
    if (strcmp(state_str, "self_checking") == 0) return ELEGOO_STATE_SELF_CHECKING;
    if (strcmp(state_str, "auto_leveling") == 0) return ELEGOO_STATE_AUTO_LEVELING;
    if (strcmp(state_str, "pid_calibrating") == 0) return ELEGOO_STATE_PID_CALIBRATING;
    if (strcmp(state_str, "resonance_testing") == 0) return ELEGOO_STATE_RESONANCE_TESTING;
    if (strcmp(state_str, "updating") == 0) return ELEGOO_STATE_UPDATING;
    if (strcmp(state_str, "file_copying") == 0) return ELEGOO_STATE_FILE_COPYING;
    if (strcmp(state_str, "file_transferring") == 0) return ELEGOO_STATE_FILE_TRANSFERRING;
    if (strcmp(state_str, "homing") == 0) return ELEGOO_STATE_HOMING;
    if (strcmp(state_str, "preheating") == 0) return ELEGOO_STATE_PREHEATING;
    if (strcmp(state_str, "filament_operating") == 0) return ELEGOO_STATE_FILAMENT_OPERATING;
    if (strcmp(state_str, "extruder_operating") == 0) return ELEGOO_STATE_EXTRUDER_OPERATING;
    if (strcmp(state_str, "exception") == 0) return ELEGOO_STATE_EXCEPTION;
    if (strcmp(state_str, "offline") == 0) return ELEGOO_STATE_OFFLINE;
    return ELEGOO_STATE_UNKNOWN;
}

static int parse_printer_substate(const char *substate_str) {
    if (strcmp(substate_str, "none") == 0) return ELEGOO_SUBSTATE_NONE;
    if (strcmp(substate_str, "homing") == 0) return ELEGOO_SUBSTATE_P_HOMING;
    if (strcmp(substate_str, "auto_leveling") == 0) return ELEGOO_SUBSTATE_P_AUTO_LEVELING;
    if (strcmp(substate_str, "printing") == 0) return ELEGOO_SUBSTATE_P_PRINTING;
    if (strcmp(substate_str, "pausing") == 0) return ELEGOO_SUBSTATE_P_PAUSING;
    if (strcmp(substate_str, "paused") == 0) return ELEGOO_SUBSTATE_P_PAUSED;
    if (strcmp(substate_str, "stopping") == 0) return ELEGOO_SUBSTATE_P_STOPPING;
    if (strcmp(substate_str, "stopped") == 0) return ELEGOO_SUBSTATE_P_STOPPED;
    if (strcmp(substate_str, "printing_complete") == 0) return ELEGOO_SUBSTATE_P_PRINTING_COMPLETED;
    return ELEGOO_SUBSTATE_UNKNOWN;
}

static void parse_json_status(const char *json_str, elegoo_printer_status_t *status) {
    memset(status, 0, sizeof(elegoo_printer_status_t));

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGW(TAG, "Failed to parse JSON response");
        return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (result == NULL) {
        result = root;
    }

    cJSON *status_obj = cJSON_GetObjectItem(result, "status");
    if (status_obj) {
        cJSON *state = cJSON_GetObjectItem(status_obj, "state");
        if (state && cJSON_IsString(state)) {
            status->status.state = parse_printer_state(state->valuestring);
        }

        cJSON *substate = cJSON_GetObjectItem(status_obj, "substate");
        if (substate && cJSON_IsString(substate)) {
            status->status.substate = parse_printer_substate(substate->valuestring);
        }

        cJSON *progress = cJSON_GetObjectItem(status_obj, "progress");
        if (progress && cJSON_IsNumber(progress)) {
            status->status.progress = progress->valueint;
            status->status.support_progress = true;
        }
    }

    cJSON *temp_obj = cJSON_GetObjectItem(result, "temperature");
    if (temp_obj) {
        cJSON *bed = cJSON_GetObjectItem(temp_obj, "bed");
        if (bed) {
            cJSON *actual = cJSON_GetObjectItem(bed, "actual");
            cJSON *target = cJSON_GetObjectItem(bed, "target");
            if (actual && cJSON_IsNumber(actual)) {
                status->temperature.bed_actual = (float)actual->valuedouble;
            }
            if (target && cJSON_IsNumber(target)) {
                status->temperature.bed_target = (float)target->valuedouble;
            }
        }

        cJSON *nozzle = cJSON_GetObjectItem(temp_obj, "nozzle");
        if (nozzle) {
            cJSON *actual = cJSON_GetObjectItem(nozzle, "actual");
            cJSON *target = cJSON_GetObjectItem(nozzle, "target");
            if (actual && cJSON_IsNumber(actual)) {
                status->temperature.nozzle_actual = (float)actual->valuedouble;
            }
            if (target && cJSON_IsNumber(target)) {
                status->temperature.nozzle_target = (float)target->valuedouble;
            }
        }
    }

    cJSON *print_obj = cJSON_GetObjectItem(result, "print");
    if (print_obj) {
        cJSON *file_name = cJSON_GetObjectItem(print_obj, "file_name");
        if (file_name && cJSON_IsString(file_name)) {
            strncpy(status->print_status.file_name, file_name->valuestring, sizeof(status->print_status.file_name) - 1);
        }

        cJSON *progress = cJSON_GetObjectItem(print_obj, "progress");
        if (progress && cJSON_IsNumber(progress)) {
            status->print_status.progress = progress->valueint;
        }

        cJSON *current_layer = cJSON_GetObjectItem(print_obj, "current_layer");
        if (current_layer && cJSON_IsNumber(current_layer)) {
            status->print_status.current_layer = current_layer->valueint;
        }

        cJSON *total_layer = cJSON_GetObjectItem(print_obj, "total_layer");
        if (total_layer && cJSON_IsNumber(total_layer)) {
            status->print_status.total_layer = total_layer->valueint;
        }

        cJSON *current_time = cJSON_GetObjectItem(print_obj, "current_time");
        if (current_time && cJSON_IsNumber(current_time)) {
            status->print_status.current_time = current_time->valueint;
        }

        cJSON *total_time = cJSON_GetObjectItem(print_obj, "total_time");
        if (total_time && cJSON_IsNumber(total_time)) {
            status->print_status.total_time = total_time->valueint;
        }

        cJSON *estimated_time = cJSON_GetObjectItem(print_obj, "estimated_time");
        if (estimated_time && cJSON_IsNumber(estimated_time)) {
            status->print_status.estimated_time = estimated_time->valueint;
        }
    }

    cJSON *progress_obj = cJSON_GetObjectItem(result, "progress");
    if (progress_obj) {
        cJSON *percent = cJSON_GetObjectItem(progress_obj, "completion");
        if (percent && cJSON_IsNumber(percent)) {
            status->status.progress = (int)(percent->valuedouble);
            status->status.support_progress = true;
        }
    }

    cJSON_Delete(root);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->user_data && evt->data_len > 0) {
                strncat((char*)evt->user_data, (char*)evt->data, evt->data_len);
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t elegoo_init(elegoo_handle_t *out_handle) {
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    elegoo_impl_t *impl = calloc(1, sizeof(elegoo_impl_t));
    if (impl == NULL) {
        return ESP_ERR_NO_MEM;
    }

    impl->mutex = xSemaphoreCreateMutex();
    if (impl->mutex == NULL) {
        free(impl);
        return ESP_ERR_NO_MEM;
    }

    impl->config.port = ELEGOO_PRINTER_DEFAULT_PORT;
    impl->config.auto_reconnect = true;
    impl->config.connection_timeout_ms = ELEGOO_PRINTER_TIMEOUT_MS;
    impl->config.printer_type = ELEGOO_PRINTER_TYPE_NEPTUNE4;

    *out_handle = (elegoo_handle_t)impl;
    return ESP_OK;
}

esp_err_t elegoo_configure(elegoo_handle_t handle, const elegoo_config_t *config) {
    if (handle == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    elegoo_impl_t *impl = (elegoo_impl_t*)handle;

    xSemaphoreTake(impl->mutex, portMAX_DELAY);
    memcpy(&impl->config, config, sizeof(elegoo_config_t));
    xSemaphoreGive(impl->mutex);

    return ESP_OK;
}

esp_err_t elegoo_connect(elegoo_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    elegoo_impl_t *impl = (elegoo_impl_t*)handle;

    xSemaphoreTake(impl->mutex, portMAX_DELAY);
    impl->connecting = true;
    xSemaphoreGive(impl->mutex);

    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d%s", impl->config.host, impl->config.port, WEBSOCKET_PATH);

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = impl->config.connection_timeout_ms,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        xSemaphoreTake(impl->mutex, portMAX_DELAY);
        impl->connecting = false;
        xSemaphoreGive(impl->mutex);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    esp_http_client_cleanup(client);

    xSemaphoreTake(impl->mutex, portMAX_DELAY);
    if (err == ESP_OK) {
        impl->connected = true;
        ESP_LOGI(TAG, "Connected to printer at %s:%d", impl->config.host, impl->config.port);
    } else {
        impl->connected = false;
        ESP_LOGW(TAG, "Failed to connect to printer at %s:%d", impl->config.host, impl->config.port);
    }
    impl->connecting = false;
    xSemaphoreGive(impl->mutex);

    if (impl->connected && impl->connected_callback) {
        impl->connected_callback(true, impl->connected_user_data);
    }

    return impl->connected ? ESP_OK : ESP_FAIL;
}

esp_err_t elegoo_disconnect(elegoo_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    elegoo_impl_t *impl = (elegoo_impl_t*)handle;

    xSemaphoreTake(impl->mutex, portMAX_DELAY);
    impl->connected = false;
    if (impl->mqtt_client) {
        esp_mqtt_client_stop(impl->mqtt_client);
        esp_mqtt_client_destroy(impl->mqtt_client);
        impl->mqtt_client = NULL;
    }
    xSemaphoreGive(impl->mutex);

    if (impl->connected_callback) {
        impl->connected_callback(false, impl->connected_user_data);
    }

    return ESP_OK;
}

bool elegoo_is_connected(elegoo_handle_t handle) {
    if (handle == NULL) {
        return false;
    }
    elegoo_impl_t *impl = (elegoo_impl_t*)handle;
    bool connected;
    xSemaphoreTake(impl->mutex, portMAX_DELAY);
    connected = impl->connected;
    xSemaphoreGive(impl->mutex);
    return connected;
}

esp_err_t elegoo_get_status(elegoo_handle_t handle, elegoo_printer_status_t *status) {
    if (handle == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    elegoo_impl_t *impl = (elegoo_impl_t*)handle;

    if (!elegoo_is_connected(handle)) {
        return ESP_FAIL;
    }

    char response_buffer[4096] = {0};

    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/api/v1/printer", impl->config.host, impl->config.port);

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = impl->config.connection_timeout_ms,
        .event_handler = http_event_handler,
        .user_data = response_buffer,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length > 0 && content_length < sizeof(response_buffer)) {
        int data_read = esp_http_client_read_response(client, response_buffer, content_length);
        if (data_read > 0) {
            response_buffer[data_read] = '\0';
            parse_json_status(response_buffer, status);
        }
    }

    esp_http_client_cleanup(client);

    xSemaphoreTake(impl->mutex, portMAX_DELAY);
    memcpy(&impl->last_status, status, sizeof(elegoo_printer_status_t));
    xSemaphoreGive(impl->mutex);

    return ESP_OK;
}

esp_err_t elegoo_refresh_status(elegoo_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    elegoo_impl_t *impl = (elegoo_impl_t*)handle;
    elegoo_printer_status_t status;

    esp_err_t err = elegoo_get_status(handle, &status);
    if (err == ESP_OK && impl->status_callback) {
        impl->status_callback(&status, impl->status_user_data);
    }

    return err;
}

esp_err_t elegoo_register_status_callback(elegoo_handle_t handle, elegoo_status_callback_t callback, void *user_data) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    elegoo_impl_t *impl = (elegoo_impl_t*)handle;
    xSemaphoreTake(impl->mutex, portMAX_DELAY);
    impl->status_callback = callback;
    impl->status_user_data = user_data;
    xSemaphoreGive(impl->mutex);

    return ESP_OK;
}

esp_err_t elegoo_register_connection_callback(elegoo_handle_t handle, elegoo_connected_callback_t callback, void *user_data) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    elegoo_impl_t *impl = (elegoo_impl_t*)handle;
    xSemaphoreTake(impl->mutex, portMAX_DELAY);
    impl->connected_callback = callback;
    impl->connected_user_data = user_data;
    xSemaphoreGive(impl->mutex);

    return ESP_OK;
}

esp_err_t elegoo_delete(elegoo_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    elegoo_impl_t *impl = (elegoo_impl_t*)handle;

    elegoo_disconnect(handle);

    if (impl->poll_task_handle) {
        vTaskDelete(impl->poll_task_handle);
    }

    if (impl->mutex) {
        vSemaphoreDelete(impl->mutex);
    }

    free(impl);
    return ESP_OK;
}
