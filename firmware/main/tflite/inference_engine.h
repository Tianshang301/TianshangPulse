#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint16_t heart_rate_bpm;
    uint8_t  blood_oxygen_pct;
    uint8_t  anomaly_flag;      // 0=正常 1=异常
    uint8_t  confidence;        // 0-100
} inference_result_t;

esp_err_t inference_engine_init(void);
esp_err_t inference_engine_run(void);
esp_err_t inference_engine_get_result(inference_result_t *out);
esp_err_t inference_engine_deinit(void);
