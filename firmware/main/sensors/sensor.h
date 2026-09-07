#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct {
    uint16_t heart_rate_bpm;      // 实时心率
    uint8_t  blood_oxygen_pct;    // 实时血氧 %
    float    raw_ir;              // 当前 IR 原始样本（归一化，供波形/特征提取）
} sensor_ppg_data_t;

typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} sensor_imu_data_t;

esp_err_t sensor_init(void);
esp_err_t sensor_sample(void);
esp_err_t sensor_get_ppg(sensor_ppg_data_t *out);
esp_err_t sensor_get_imu(sensor_imu_data_t *out);
esp_err_t sensor_deinit(void);
