#pragma once

#include "esp_err.h"

typedef enum {
    POWER_MODE_ACTIVE,       // 主动监测模式 ≤30mA
    POWER_MODE_LIGHT_SLEEP,  // 轻睡眠
    POWER_MODE_STANDBY,      // 待机 ≤500μA
} power_mode_t;

esp_err_t power_manager_init(void);
esp_err_t power_set_mode(power_mode_t mode);
power_mode_t power_get_mode(void);
esp_err_t power_manager_deinit(void);
