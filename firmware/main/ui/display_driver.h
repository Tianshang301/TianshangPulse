#pragma once

#include "esp_err.h"

/* ILI9341 SPI panel + LVGL port wiring. Only compiled when
 * CONFIG_DISPLAY_ENABLE is set; ui_manager.c guards the calls. */
esp_err_t display_driver_init(void);
esp_err_t display_driver_deinit(void);