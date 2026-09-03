#include <string.h>
#include "ble/offline_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "config.h"

#define TAG "offline_cache"

static ble_anomaly_event_t *s_ring = NULL;
static volatile size_t s_head = 0;
static volatile size_t s_count = 0;

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
    s_ring[s_head] = *event;
    s_head = (s_head + 1) % KMaxOfflineEvents;
    if (s_count < KMaxOfflineEvents) s_count++;
    return ESP_OK;
}

esp_err_t offline_cache_flush(void)
{
    s_head = 0;
    s_count = 0;
    return ESP_OK;
}

esp_err_t offline_cache_get_all(ble_anomaly_event_t *out, size_t cap, size_t *count)
{
    if (!out || !count || !s_ring) return ESP_ERR_INVALID_STATE;
    size_t n = (s_count < cap) ? s_count : cap;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_ring[(s_head + s_count - n + i) % KMaxOfflineEvents];
    }
    *count = n;
    return ESP_OK;
}

size_t offline_cache_count(void)
{
    return s_count;
}
