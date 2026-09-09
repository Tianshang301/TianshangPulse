#pragma once

#include "esp_err.h"

typedef enum {
    POWER_MODE_ACTIVE,       // 主动监测：CPU 全速、禁止 light sleep（≤30mA）
    POWER_MODE_LIGHT_SLEEP, // 浅睡：释放频率锁，允许 auto-DFS + light sleep
    POWER_MODE_STANDBY,     // 待机：深度睡眠 + 定时器唤醒（≤500μA）
} power_mode_t;

esp_err_t power_manager_init(void);
esp_err_t power_set_mode(power_mode_t mode);
power_mode_t power_get_mode(void);
const char *power_mode_name(power_mode_t mode);
esp_err_t power_manager_deinit(void);
