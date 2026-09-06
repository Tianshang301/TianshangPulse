#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t heart_rate_bpm;
    uint8_t  blood_oxygen_pct;
    uint8_t  anomaly_flag;      // 0=正常 1=异常
    uint8_t  confidence;        // 0-100；门控分级映射见 signal_gate.h
} inference_result_t;

esp_err_t inference_engine_init(void);
esp_err_t inference_engine_run(void);
/* 带门控信息的推理：gated_level 与 motion_energy 由 signal_gate 提供；
 * 高运动/低 SQI 时内部决定是否执行推理并抑制 anomaly_flag。 */
esp_err_t inference_engine_run_gated(int gated_level);
esp_err_t inference_engine_get_result(inference_result_t *out);
esp_err_t inference_engine_deinit(void);

#ifdef __cplusplus
}
#endif
