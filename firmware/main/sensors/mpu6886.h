#pragma once

#include "esp_err.h"
#include "sensors/sensor.h"

#define MPU6886_I2C_ADDR     0x68

esp_err_t mpu6886_init(void);
esp_err_t mpu6886_read_imu(sensor_imu_data_t *out);
float mpu6886_accel_g(int16_t raw);   // raw LSB -> g（当前 +-8g 量程）
float mpu6886_gyro_dps(int16_t raw);  // raw LSB -> dps（当前 +-2000dps 量程）
esp_err_t mpu6886_deinit(void);
