#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    BLE_EVENT_ANOMALY_HR,       // 心率异常
    BLE_EVENT_ANOMALY_SPO2,     // 血氧异常
    BLE_EVENT_MODEL_SWITCH,     // 模型切换
} ble_event_type_t;

typedef struct {
    ble_event_type_t type;
    uint64_t timestamp_ms;
    uint8_t confidence;         // 0-100
} ble_anomaly_event_t;

esp_err_t ble_gatt_server_init(void);
esp_err_t ble_notify_heart_rate(uint16_t bpm);
esp_err_t ble_notify_spo2(uint8_t pct);
esp_err_t ble_notify_anomaly(const ble_anomaly_event_t *event);
esp_err_t ble_deinit(void);
