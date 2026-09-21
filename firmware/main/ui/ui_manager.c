#include "ui/ui_manager.h"
#include "esp_log.h"
#include "esp_check.h"
#include "ui/display_config.h"

#define TAG "ui"

#if KDisplayEnable

#include "ui/display_driver.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

/* Minimal status screen for bring-up: identity + HR/AF placeholders.
 * Live values will be wired from inference results in a later iteration. */
static void ui_create_status_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "TianshangPulse");
    lv_obj_set_style_text_color(title, lv_color_hex(0x66D9EF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *hr = lv_label_create(scr);
    lv_label_set_text(hr, "HR   -- bpm");
    lv_obj_set_style_text_color(hr, lv_color_hex(0xF8F8F2), 0);
    lv_obj_set_style_text_font(hr, &lv_font_montserrat_24, 0);
    lv_obj_align(hr, LV_ALIGN_CENTER, 0, -24);

    lv_obj_t *af = lv_label_create(scr);
    lv_label_set_text(af, "AF monitor: running");
    lv_obj_set_style_text_color(af, lv_color_hex(0xA6E22E), 0);
    lv_obj_align(af, LV_ALIGN_CENTER, 0, 32);
}

#endif /* KDisplayEnable */

esp_err_t ui_init(void)
{
#if KDisplayEnable
    ESP_RETURN_ON_ERROR(display_driver_init(), TAG, "display init");

    lvgl_port_lock(0);
    ui_create_status_screen();
    lvgl_port_unlock();

    ESP_LOGI(TAG, "LVGL UI ready (ILI9341 %dx%d)", KDisplayHorRes, KDisplayVerRes);
    return ESP_OK;
#else
    ESP_LOGI(TAG, "LVGL UI init (skeleton, display disabled)");
    return ESP_OK;
#endif
}

esp_err_t ui_deinit(void)
{
#if KDisplayEnable
    return display_driver_deinit();
#else
    return ESP_OK;
#endif
}
