#include "ui/display_driver.h"

#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_check.h"
#include "esp_log.h"
#include "hal/spi_types.h"
#include "esp_lcd_ili9341.h"
#include "esp_lvgl_port.h"
#include "ui/display_config.h"

#define TAG "display"

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static lv_display_t *s_lvgl_disp = NULL;
static bool s_bus_ready = false;

esp_err_t display_driver_init(void)
{
    if (s_lvgl_disp != NULL) {
        return ESP_OK;                          /* already initialized */
    }

    /* 1. SPI bus (SPI2_HOST = FSPI on S3; pins from display_config.h) */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = KDisplaySpiMosiGpio,
        .miso_io_num = -1,
        .sclk_io_num = KDisplaySpiSckGpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = KDisplayDrawBufPix * (KDisplayBitsPerPixel / 8),
    };
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_bus_ready = true;

    /* 2. Panel IO: 8-bit cmd / 8-bit param, mode 0.
     * trans_queue_depth = 0 -> synchronous transfers (see display_config.h). */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = KDisplayDcGpio,
        .cs_gpio_num = KDisplayCsGpio,
        .pclk_hz = KDisplaySpiClockHz,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 0,
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                   &io_cfg, &s_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "panel io init failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* 3. ILI9341 panel.
     * NOT HARDWARE-VERIFIED yet: rgb_ele_order (BGR is the common breakout
     * default), mirror/swap_xy orientation and invert may need a one-line
     * change on the real board -- all in this block, see display_config.h. */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = KDisplayRstGpio,      /* -1 = no reset pin */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = KDisplayBitsPerPixel,
    };
    ret = esp_lcd_new_panel_ili9341(s_io_handle, &panel_cfg, &s_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ili9341 panel init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    ret = esp_lcd_panel_reset(s_panel_handle);
    if (ret != ESP_OK) goto fail;
    ret = esp_lcd_panel_init(s_panel_handle);
    if (ret != ESP_OK) goto fail;
    (void)esp_lcd_panel_invert_color(s_panel_handle, false);
    (void)esp_lcd_panel_mirror(s_panel_handle, false, false);
    (void)esp_lcd_panel_swap_xy(s_panel_handle, false);
    ret = esp_lcd_panel_disp_on_off(s_panel_handle, true);
    if (ret != ESP_OK) goto fail;

    /* 4. Backlight: plain on/off for bring-up (PWM dimming is a later
     * iteration, see docs/POWER_BUDGET.md). */
    if (KDisplayBlGpio >= 0) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = 1ULL << (unsigned)KDisplayBlGpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&bl_cfg);
        gpio_set_level(KDisplayBlGpio, 1);      /* active-high breakout */
    }

    /* 5. LVGL port: default task/timer config; full-frame draw buffer in
     * PSRAM, streamed to SPI DMA through a small SRAM chunk. */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "lvgl port init failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_io_handle,
        .panel_handle = s_panel_handle,
        .buffer_size = KDisplayDrawBufPix,
        .trans_size = KDisplayTransBytes,
        .double_buffer = false,
        .hres = KDisplayHorRes,
        .vres = KDisplayVerRes,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,                  /* buffer is in PSRAM */
            .buff_spiram = true,
            .swap_bytes = false,                /* tune on real board */
        },
    };
    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (s_lvgl_disp == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        goto fail;
    }

    ESP_LOGI(TAG, "ILI9341 %dx%d ready (SPI2 @%dHz, PSRAM draw buffer)",
             KDisplayHorRes, KDisplayVerRes, KDisplaySpiClockHz);
    return ESP_OK;

fail:
    display_driver_deinit();
    return ret == ESP_OK ? ESP_FAIL : ret;
}

esp_err_t display_driver_deinit(void)
{
    if (s_lvgl_disp != NULL) {
        lvgl_port_remove_disp(s_lvgl_disp);
        s_lvgl_disp = NULL;
    }
    lvgl_port_deinit();

    if (s_panel_handle != NULL) {
        esp_lcd_panel_del(s_panel_handle);
        s_panel_handle = NULL;
    }
    if (s_io_handle != NULL) {
        esp_lcd_panel_io_del(s_io_handle);
        s_io_handle = NULL;
    }
    if (s_bus_ready) {
        spi_bus_free(SPI2_HOST);
        s_bus_ready = false;
    }
    if (KDisplayBlGpio >= 0) {
        gpio_reset_pin(KDisplayBlGpio);
    }
    return ESP_OK;
}