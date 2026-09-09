#include "power/power_manager.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "config.h"

#define TAG "power"

static power_mode_t s_mode = POWER_MODE_ACTIVE;

static esp_pm_lock_handle_t s_active_lock = NULL;

esp_err_t power_manager_init(void)
{
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = KPlatformCpuMaxMhz,
        .min_freq_mhz = KPlatformCpuMinMhz,
        .light_sleep_enable = true,
    };
    esp_err_t ret = esp_pm_configure(&pm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "active", &s_active_lock);
    if (!s_active_lock) {
        ESP_LOGE(TAG, "pm lock create failed");
        return ESP_FAIL;
    }

    /* 启动即 ACTIVE：持有频率锁，CPU 全速、禁止 light sleep */
    esp_pm_lock_acquire(s_active_lock);
    s_mode = POWER_MODE_ACTIVE;
    ESP_LOGI(TAG, "power manager init, mode=%s", power_mode_name(s_mode));
    return ESP_OK;
}

esp_err_t power_set_mode(power_mode_t mode)
{
    if (mode == s_mode) return ESP_OK;

    switch (mode) {
    case POWER_MODE_ACTIVE:
        esp_pm_lock_acquire(s_active_lock);   /* 全速、禁止 light sleep */
        break;
    case POWER_MODE_LIGHT_SLEEP:
        esp_pm_lock_release(s_active_lock);   /* 允许 auto-DFS + light sleep */
        break;
    case POWER_MODE_STANDBY: {
        ESP_LOGI(TAG, "entering standby (deep sleep %us wake)", (unsigned)KPowerStandbyWakeSec);
        esp_sleep_enable_timer_wakeup((uint64_t)KPowerStandbyWakeSec * 1000000ULL);
        esp_deep_sleep_start();               /* 不返回：唤醒即复位 */
        break;
    }
    default:
        return ESP_ERR_INVALID_ARG;
    }

    s_mode = mode;
    ESP_LOGI(TAG, "mode -> %s", power_mode_name(s_mode));
    return ESP_OK;
}

power_mode_t power_get_mode(void)
{
    return s_mode;
}

const char *power_mode_name(power_mode_t mode)
{
    switch (mode) {
    case POWER_MODE_ACTIVE:      return "ACTIVE";
    case POWER_MODE_LIGHT_SLEEP: return "LIGHT_SLEEP";
    case POWER_MODE_STANDBY:     return "STANDBY";
    default:                     return "?";
    }
}

esp_err_t power_manager_deinit(void)
{
    if (s_active_lock) {
        if (s_mode == POWER_MODE_ACTIVE) {
            esp_pm_lock_release(s_active_lock);
        }
        esp_pm_lock_delete(s_active_lock);
        s_active_lock = NULL;
    }
    return ESP_OK;
}
