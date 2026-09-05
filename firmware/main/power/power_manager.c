#include "power/power_manager.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "config.h"

#define TAG "power"

static power_mode_t s_mode = POWER_MODE_ACTIVE;

static esp_pm_lock_handle_t s_light_sleep_lock = NULL;

esp_err_t power_manager_init(void)
{
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = KPlatformCpuMaxMhz,
        .min_freq_mhz = KPlatformCpuMinMhz,
        .light_sleep_enable = true,
    };
    esp_err_t ret = esp_pm_configure(&pm_cfg);
    if (ret == ESP_OK) {
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "active", &s_light_sleep_lock);
    }
    ESP_LOGI(TAG, "power manager init done");
    return ret;
}

esp_err_t power_set_mode(power_mode_t mode)
{
    s_mode = mode;
    return ESP_OK;
}

power_mode_t power_get_mode(void)
{
    return s_mode;
}

esp_err_t power_manager_deinit(void)
{
    if (s_light_sleep_lock) {
        esp_pm_lock_delete(s_light_sleep_lock);
    }
    return ESP_OK;
}
