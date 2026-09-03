#include "ui/ui_manager.h"
#include "esp_log.h"

#define TAG "ui"

esp_err_t ui_init(void)
{
    ESP_LOGI(TAG, "LVGL UI init (skeleton)");
    return ESP_OK;
}

esp_err_t ui_deinit(void)
{
    return ESP_OK;
}
