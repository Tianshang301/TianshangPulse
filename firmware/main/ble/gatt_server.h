#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
/* 线格式类型（ble_event_type_t / ble_anomaly_event_t / ble_user_config_t）与编解码
 * 纯函数已迁至 ble/protocol.h —— 此处转出以便既有包含方无感（行为不变，P4）。 */
#include "ble/protocol.h"

esp_err_t ble_gatt_server_init(void);
esp_err_t ble_notify_heart_rate(uint16_t bpm);
esp_err_t ble_notify_spo2(uint8_t pct);
esp_err_t ble_notify_anomaly(const ble_anomaly_event_t *event);
const ble_user_config_t *ble_get_user_config(void);
uint8_t ble_get_model_index(void);   // 0=A(默认) 1=B
esp_err_t ble_deinit(void);
