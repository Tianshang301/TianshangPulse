#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "ble/gatt_server.h"

esp_err_t offline_cache_init(void);
esp_err_t offline_cache_push(const ble_anomaly_event_t *event);
esp_err_t offline_cache_flush(void);
esp_err_t offline_cache_get_all(ble_anomaly_event_t *out, size_t cap, size_t *count);
size_t offline_cache_count(void);
