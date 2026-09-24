#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "ble/gatt_server.h"

esp_err_t offline_cache_init(void);
esp_err_t offline_cache_push(const ble_anomaly_event_t *event);
/* 预览最旧 <=cap 条（不删除），最旧优先。用于 0xFFF4 Read：
 * APP ACK（Write）后再按实际交付条数 offline_cache_pop() 精确清除，
 * 避免单批容量(20) < 缓存上限(100) 时未读事件被误清。 */
esp_err_t offline_cache_peek_batch(ble_anomaly_event_t *out, size_t cap, size_t *count);
/* 删除最旧 n 条（n 超出现有条数时自动裁剪到现有条数）。 */
esp_err_t offline_cache_pop(size_t n);
esp_err_t offline_cache_flush(void);
size_t offline_cache_count(void);