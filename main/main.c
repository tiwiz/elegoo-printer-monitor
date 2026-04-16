#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_gc9a01.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "elegoo_printer.h"
#include "web_config.h"

static const char *TAG = "printer-monitor";

#define LCD_HOST SPI2_HOST

#define GPIO_LCD_MOSI 7
#define GPIO_LCD_SCK 6
#define GPIO_LCD_CS 10
#define GPIO_LCD_DC 4
#define GPIO_LCD_RES 5
#define GPIO_LCD_BL 8

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240

static lv_display_t *display;
static SemaphoreHandle_t lvgl_mux;
static elegoo_handle_t elegoo_handle;
static web_config_ctx_t *web_config_ctx;
static bool printer_connected = false;

typedef enum {
    UI_STATE_CONFIG,
    UI_STATE_CONNECTING_WIFI,
    UI_STATE_CONNECTING_PRINTER,
    UI_STATE_IDLE,
    UI_STATE_PRINTING,
    UI_STATE_ERROR
} ui_state_t;

static ui_state_t current_state = UI_STATE_CONFIG;
static ui_state_t previous_state = UI_STATE_CONFIG;

typedef enum {
    EXPRESSION_NEUTRAL,
    EXPRESSION_HAPPY,
    EXPRESSION_PRINTING,
    EXPRESSION_ERROR,
    EXPRESSION_COMPLETE
} expression_t;

static expression_t current_expression = EXPRESSION_NEUTRAL;

static lv_obj_t *face_container;
static lv_obj_t *progress_arc;
static lv_obj_t *temp_bed_label;
static lv_obj_t *temp_nozzle_label;
static lv_obj_t *progress_label;
static lv_obj_t *time_label;
static lv_obj_t *status_label;
static lv_obj_t *wifi_icon;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, unsigned char *data) {
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int x_start = area->x1;
    int x_end = area->x2;
    int y_start = area->y1;
    int y_end = area->y2;
    lv_draw_sw_rgb565_swap(data, (x_end - x_start + 1) * (y_end - y_start + 1));
    esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end + 1, y_end + 1, data);
    lv_display_flush_ready(disp);
}

static void increase_lvgl_tick(void *arg) {
    lv_tick_inc(2);
}

static esp_err_t display_init(esp_lcd_panel_handle_t *panel_handle) {
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = GPIO_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = GPIO_LCD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_LCD_RES,
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(LCD_HOST, &panel_cfg, panel_handle));

    gpio_set_direction(GPIO_LCD_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_LCD_BL, 1);

    ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_handle, true));

    return ESP_OK;
}

static void draw_expression(expression_t expr) {
    if (expr == current_expression) return;
    current_expression = expr;

    lv_obj_clean(face_container);

    lv_style_t eye_style;
    lv_style_init(&eye_style);
    lv_style_set_bg_color(&eye_style, lv_color_hex(0xffffff));
    lv_style_set_radius(&eye_style, LV_RADIUS_CIRCLE);

    lv_obj_t *left_eye = lv_obj_create(face_container);
    lv_obj_set_size(left_eye, 25, 25);
    lv_obj_align(left_eye, LV_ALIGN_CENTER, -30, -20);
    lv_obj_add_style(left_eye, &eye_style, 0);

    lv_obj_t *right_eye = lv_obj_create(face_container);
    lv_obj_set_size(right_eye, 25, 25);
    lv_obj_align(right_eye, LV_ALIGN_CENTER, 30, -20);
    lv_obj_add_style(right_eye, &eye_style, 0);

    lv_obj_t *mouth = lv_obj_create(face_container);
    lv_style_t mouth_style;
    lv_style_init(&mouth_style);

    switch (expr) {
        case EXPRESSION_HAPPY:
        case EXPRESSION_COMPLETE:
            lv_obj_set_size(mouth, 50, 25);
            lv_obj_align(mouth, LV_ALIGN_CENTER, 0, 20);
            lv_style_set_bg_color(&mouth_style, lv_color_hex(0x4ade80));
            lv_style_set_radius(&mouth_style, LV_RADIUS_CIRCLE);
            break;
        case EXPRESSION_PRINTING:
            lv_obj_set_size(mouth, 40, 12);
            lv_obj_align(mouth, LV_ALIGN_CENTER, 0, 25);
            lv_style_set_bg_color(&mouth_style, lv_color_hex(0xfbbf24));
            lv_style_set_radius(&mouth_style, 6);
            break;
        case EXPRESSION_ERROR:
            lv_obj_set_size(mouth, 50, 15);
            lv_obj_align(mouth, LV_ALIGN_CENTER, 0, 25);
            lv_style_set_bg_color(&mouth_style, lv_color_hex(0xef4444));
            lv_style_set_radius(&mouth_style, 4);
            break;
        default:
            lv_obj_set_size(mouth, 40, 6);
            lv_obj_align(mouth, LV_ALIGN_CENTER, 0, 25);
            lv_style_set_bg_color(&mouth_style, lv_color_hex(0x94a3b8));
            lv_style_set_radius(&mouth_style, 3);
            break;
    }
    lv_obj_add_style(mouth, &mouth_style, 0);
}

static void draw_wifi_indicator(bool connected) {
    lv_obj_clean(wifi_icon);
    lv_style_t dot_style;
    lv_style_init(&dot_style);
    lv_style_set_bg_color(&dot_style, connected ? lv_color_hex(0x4ade80) : lv_color_hex(0x6b7280));
    lv_style_set_radius(&dot_style, LV_RADIUS_CIRCLE);
    lv_obj_add_style(wifi_icon, &dot_style, 0);
}

static void create_config_ui(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Setup Required");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    face_container = lv_obj_create(scr);
    lv_obj_set_size(face_container, 160, 100);
    lv_obj_align(face_container, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_opa(face_container, LV_OPA_TRANSP, 0);
    draw_expression(EXPRESSION_NEUTRAL);

    status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x94a3b8), 0);
    lv_label_set_text(status_label, "Connect to WiFi\n\"ElegooMonitor\"\nThen open browser");
    lv_label_set_recolor(status_label, true);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6b7280), 0);
    lv_label_set_text(hint, "Then open http://192.168.4.1");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static void create_ui(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);

    wifi_icon = lv_obj_create(scr);
    lv_obj_set_size(wifi_icon, 12, 12);
    lv_obj_align(wifi_icon, LV_ALIGN_TOP_RIGHT, -10, 10);
    draw_wifi_indicator(false);

    face_container = lv_obj_create(scr);
    lv_obj_set_size(face_container, 160, 100);
    lv_obj_center(face_container);
    lv_obj_set_style_bg_opa(face_container, LV_OPA_TRANSP, 0);

    progress_arc = lv_arc_create(scr);
    lv_obj_set_size(progress_arc, 200, 200);
    lv_arc_set_rotation(progress_arc, 135);
    lv_arc_set_range(progress_arc, 0, 100);
    lv_arc_set_value(progress_arc, 0);
    lv_obj_center(progress_arc);

    lv_style_t arc_bg_style;
    lv_style_init(&arc_bg_style);
    lv_style_set_arc_color(&arc_bg_style, lv_color_hex(0x2d2d44));
    lv_style_set_arc_width(&arc_bg_style, 10);
    lv_obj_add_style(progress_arc, &arc_bg_style, LV_PART_MAIN);

    lv_style_t arc_indicator_style;
    lv_style_init(&arc_indicator_style);
    lv_style_set_arc_color(&arc_indicator_style, lv_color_hex(0x3b82f6));
    lv_style_set_arc_width(&arc_indicator_style, 10);
    lv_obj_add_style(progress_arc, &arc_indicator_style, LV_PART_INDICATOR);

    lv_obj_t *temp_container = lv_obj_create(scr);
    lv_obj_set_size(temp_container, 220, 35);
    lv_obj_align(temp_container, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_style_t temp_style;
    lv_style_init(&temp_style);
    lv_style_set_bg_color(&temp_style, lv_color_hex(0x1a1a2e));
    lv_style_set_radius(&temp_style, 8);
    lv_style_set_pad_all(&temp_style, 4);
    lv_obj_add_style(temp_container, &temp_style, 0);

    temp_bed_label = lv_label_create(temp_container);
    lv_label_set_text(temp_bed_label, "BED: --°C");
    lv_obj_align(temp_bed_label, LV_ALIGN_LEFT_MID, 8, 0);

    temp_nozzle_label = lv_label_create(temp_container);
    lv_label_set_text(temp_nozzle_label, "NOZ: --°C");
    lv_obj_align(temp_nozzle_label, LV_ALIGN_RIGHT_MID, -8, 0);

    progress_label = lv_label_create(scr);
    lv_obj_set_style_text_font(progress_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(progress_label, "0%");
    lv_obj_align(progress_label, LV_ALIGN_CENTER, 0, 0);

    status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x94a3b8), 0);
    lv_label_set_text(status_label, "Connecting...");
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 10);

    time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x6b7280), 0);
    lv_label_set_text(time_label, "ETA: --:--");
    lv_obj_align(time_label, LV_ALIGN_BOTTOM_MID, 0, -60);
}

static void update_ui_from_status(const elegoo_printer_status_t *status) {
    if (xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(100)) == pdTRUE) {
        char buf[64];

        if (status->status.state == ELEGOO_STATE_PRINTING) {
            snprintf(buf, sizeof(buf), "%d%%", status->status.progress);
            lv_label_set_text(progress_label, buf);
            lv_arc_set_value(progress_arc, status->status.progress);
            draw_expression(EXPRESSION_PRINTING);
            current_state = UI_STATE_PRINTING;
        } else if (status->status.state == ELEGOO_STATE_PREHEATING) {
            lv_label_set_text(progress_label, "HEAT");
            lv_arc_set_value(progress_arc, 0);
            draw_expression(EXPRESSION_NEUTRAL);
        } else if (status->status.state == ELEGOO_STATE_IDLE) {
            lv_label_set_text(progress_label, "IDLE");
            lv_arc_set_value(progress_arc, 0);
            draw_expression(EXPRESSION_NEUTRAL);
            current_state = UI_STATE_IDLE;
        } else if (status->status.state == ELEGOO_STATE_EXCEPTION) {
            lv_label_set_text(progress_label, "ERR");
            draw_expression(EXPRESSION_ERROR);
            current_state = UI_STATE_ERROR;
        } else {
            snprintf(buf, sizeof(buf), "%d%%", status->status.progress);
            lv_label_set_text(progress_label, buf);
            lv_arc_set_value(progress_arc, status->status.progress);
            draw_expression(EXPRESSION_HAPPY);
        }

        snprintf(buf, sizeof(buf), "BED: %.0f/%.0f°C",
                 status->temperature.bed_actual, status->temperature.bed_target);
        lv_label_set_text(temp_bed_label, buf);

        snprintf(buf, sizeof(buf), "NOZ: %.0f/%.0f°C",
                 status->temperature.nozzle_actual, status->temperature.nozzle_target);
        lv_label_set_text(temp_nozzle_label, buf);

        if (status->print_status.estimated_time > 0) {
            int remaining = status->print_status.estimated_time - status->print_status.current_time;
            if (remaining < 0) remaining = 0;
            int hours = remaining / 3600;
            int mins = (remaining % 3600) / 60;
            snprintf(buf, sizeof(buf), "ETA: %d:%02d", hours, mins);
        } else {
            snprintf(buf, sizeof(buf), "ETA: --:--");
        }
        lv_label_set_text(time_label, buf);

        snprintf(buf, sizeof(buf), "%s", strlen(status->print_status.file_name) > 0 ?
                 status->print_status.file_name : elegoo_state_to_string(status->status.state));
        lv_label_set_text(status_label, buf);

        xSemaphoreGive(lvgl_mux);
    }
}

static void on_printer_status(const elegoo_printer_status_t *status, void *user_data) {
    update_ui_from_status(status);
}

static void on_printer_connected(bool connected, void *user_data) {
    printer_connected = connected;
    if (xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (connected) {
            lv_label_set_text(status_label, "Printer Connected");
            draw_wifi_indicator(true);
        } else {
            lv_label_set_text(status_label, "Printer Disconnected");
            draw_wifi_indicator(false);
        }
        xSemaphoreGive(lvgl_mux);
    }
}

static esp_err_t wifi_connect_sta(const char *ssid, const char *password) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi connecting to %s...", ssid);
    return ESP_OK;
}

static void printer_task(void *pvParameter) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(2000);

    web_config_data_t config;
    esp_err_t err = web_config_get_saved(&config);

    if (err != ESP_OK || !config.configured) {
        ESP_LOGW(TAG, "No configuration saved, starting AP mode");
        current_state = UI_STATE_CONFIG;
        vTaskDelete(NULL);
        return;
    }

    if (xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_label_set_text(status_label, "Connecting WiFi...");
        xSemaphoreGive(lvgl_mux);
    }

    wifi_connect_sta(config.wifi_ssid, config.wifi_password);

    vTaskDelay(pdMS_TO_TICKS(500));

    if (xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_label_set_text(status_label, "Connecting Printer...");
        xSemaphoreGive(lvgl_mux);
    }

    elegoo_config_t elegoo_config = {
        .port = config.printer_port,
        .printer_type = (elegoo_printer_type_t)config.printer_type,
        .auto_reconnect = true,
        .connection_timeout_ms = 5000,
    };
    strncpy(elegoo_config.host, config.printer_host, sizeof(elegoo_config.host) - 1);
    strncpy(elegoo_config.printer_name, "Elegoo Printer", sizeof(elegoo_config.printer_name) - 1);
    strncpy(elegoo_config.printer_model, web_config_printer_type_to_string(config.printer_type), sizeof(elegoo_config.printer_model) - 1);
    strncpy(elegoo_config.printer_brand, "Elegoo", sizeof(elegoo_config.printer_brand) - 1);

    elegoo_configure(elegoo_handle, &elegoo_config);
    elegoo_register_status_callback(elegoo_handle, on_printer_status, NULL);
    elegoo_register_connection_callback(elegoo_handle, on_printer_connected, NULL);

    if (elegoo_connect(elegoo_handle) == ESP_OK) {
        ESP_LOGI(TAG, "Connected to printer");
        printer_connected = true;
    } else {
        ESP_LOGW(TAG, "Failed to connect to printer, will retry");
    }

    while (1) {
        if (elegoo_is_connected(elegoo_handle)) {
            elegoo_printer_status_t status;
            if (elegoo_get_status(elegoo_handle, &status) == ESP_OK) {
                update_ui_from_status(&status);
            }
        }
        vTaskDelayUntil(&last_wake_time, interval);
    }
}

static void lvgl_task(void *pvParameter) {
    while (1) {
        if (xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGive(lvgl_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void config_task(void *pvParameter) {
    bool configured = false;

    while (!configured) {
        web_config_is_configured(&configured);
        if (!configured) {
            ESP_LOGI(TAG, "Waiting for configuration...");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    ESP_LOGI(TAG, "Configuration complete, restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void app_main(void) {
    ESP_LOGI(TAG, "Elegoo Printer Monitor Starting...");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_lcd_panel_handle_t panel_handle;
    ESP_ERROR_CHECK(display_init(&panel_handle));

    lvgl_mux = xSemaphoreCreateMutex();
    lv_init();

    uint8_t *buf1 = heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2, MALLOC_CAP_DMA);
    uint8_t *buf2 = heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2, MALLOC_CAP_DMA);

    display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_buffers(display, buf1, buf2, DISPLAY_WIDTH * DISPLAY_HEIGHT * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display, panel_handle);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    const esp_timer_create_args_t lvgl_tick_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_args, &lvgl_tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick, 2000));

    web_config_data_t saved_config;
    bool is_configured = false;
    web_config_is_configured(&is_configured);

    if (is_configured && web_config_get_saved(&saved_config) == ESP_OK) {
        ESP_LOGI(TAG, "Configuration found, starting normal mode");
        create_ui();

        ESP_ERROR_CHECK(elegoo_init(&elegoo_handle));
        xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 2, NULL);
        xTaskCreate(printer_task, "printer", 8192, NULL, 3, NULL);
    } else {
        ESP_LOGI(TAG, "No configuration, starting AP mode");
        create_config_ui();

        wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

        ESP_ERROR_CHECK(web_config_init(&web_config_ctx));
        ESP_ERROR_CHECK(web_config_start_ap(web_config_ctx));

        xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 2, NULL);
        xTaskCreate(config_task, "config_monitor", 4096, NULL, 3, NULL);
    }

    ESP_LOGI(TAG, "Printer monitor initialized");
}
