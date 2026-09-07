#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    BLE_EVENT_ANOMALY_HR,       // 心率异常
    BLE_EVENT_ANOMALY_SPO2,     // 血氧异常
    BLE_EVENT_MODEL_SWITCH,     // 模型切换
    BLE_EVENT_AF_ANOMALY = 3,   // AF 异常（房颤检出，匹配 PROTOCOL.md §4）
} ble_event_type_t;

typedef struct {
    ble_event_type_t type;
    uint64_t timestamp_ms;
    uint8_t confidence;         // 0-100
} ble_anomaly_event_t;

/* APP→Watch 用户配置（PROTOCOL.md §3，0xFFF1） */
typedef struct {
    uint16_t hr_threshold_bpm;
    uint8_t  spo2_threshold_pct;
    uint8_t  gender;            // 0=女 1=男
    uint16_t age;
} ble_user_config_t;

esp_err_t ble_gatt_server_init(void);
esp_err_t ble_notify_heart_rate(uint16_t bpm);
esp_err_t ble_notify_spo2(uint8_t pct);
esp_err_t ble_notify_anomaly(const ble_anomaly_event_t *event);
const ble_user_config_t *ble_get_user_config(void);
uint8_t ble_get_model_index(void);   // 0=A(默认) 1=B
esp_err_t ble_deinit(void);
