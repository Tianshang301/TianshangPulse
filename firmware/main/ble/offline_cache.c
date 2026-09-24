#include <string.h>
#include "freertos/FreeRTOS.h"
#include "ble/offline_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "config.h"

#define TAG "offline_cache"

static ble_anomaly_event_t *s_ring = NULL;
static size_t s_head = 0;      // 下一写入位置
static size_t s_count = 0;     // 当前缓存条数

/* inference task（push）与 NimBLE host task（peek/pop）并发访问；
 * 操作均为 <=100 条 x 16B 小拷贝，用短临界区保护（微秒级）。 */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t offline_cache_init(void)
{
    if (s_ring) return ESP_OK;
    s_ring = heap_caps_malloc(KMaxOfflineEvents * sizeof(ble_anomaly_event_t),
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) {
        ESP_LOGE(TAG, "PSRAM alloc failed for ring buffer");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t offline_cache_push(const ble_anomaly_event_t *event)
{
    if (!event || !s_ring) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lock);
    s_ring[s_head] = *event;
    s_head = (s_head + 1) % KMaxOfflineEvents;
    if (s_count < KMaxOfflineEvents) s_count++;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t offline_cache_peek_batch(ble_anomaly_event_t *out, size_t cap, size_t *count)
{
    if (!out || !count || !s_ring) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lock);
    size_t n = (s_count < cap) ? s_count : cap;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_ring[(s_head + s_count - n + i) % KMaxOfflineEvents];  // 最旧优先
    }
    portEXIT_CRITICAL(&s_lock);
    *count = n;
    return ESP_OK;
}

esp_err_t offline_cache_pop(size_t n)
{
    if (!s_ring) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lock);
    /* 删最旧 n 条：head（下一写入位）不动，仅回退计数 */
    s_count = (n >= s_count) ? 0 : s_count - n;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t offline_cache_flush(void)
{
    portENTER_CRITICAL(&s_lock);
    s_head = 0;
    s_count = 0;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

size_t offline_cache_count(void)
{
    portENTER_CRITICAL(&s_lock);
    size_t c = s_count;
    portEXIT_CRITICAL(&s_lock);
    return c;
}