#pragma once

#include "esp_err.h"
#include "sensors/sensor.h"

#define MAX30102_I2C_ADDR     0x57
#define MAX30102_REG_MODE_CFG 0x09
#define MAX30102_REG_SPO2_CFG 0x0A

esp_err_t max30102_init(void);
esp_err_t max30102_read_ppg(sensor_ppg_data_t *out);
esp_err_t max30102_deinit(void);
