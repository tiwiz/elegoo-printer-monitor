#pragma once

#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"

esp_err_t esp_lcd_new_panel_gc9a01(spi_host_device_t host, const esp_lcd_panel_dev_config_t *config, esp_lcd_panel_handle_t *ret_panel);
