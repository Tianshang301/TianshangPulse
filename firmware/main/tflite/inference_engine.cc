#include <string.h>
#include "tflite/inference_engine.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "config.h"

#define TAG "tflite"

static uint8_t *s_arena = NULL;
static inference_result_t s_result = {0};

esp_err_t inference_engine_init(void)
{
    s_arena = (uint8_t *)heap_caps_malloc(
        KTensorArenaSize,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (!s_arena) {
        ESP_LOGE(TAG, "arena alloc failed, need %d bytes PSRAM", KTensorArenaSize);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "arena allocated at %p size=%d", s_arena, KTensorArenaSize);
    return ESP_OK;
}

esp_err_t inference_engine_run(void)
{
    memset(&s_result, 0, sizeof(s_result));
    return ESP_OK;
}

esp_err_t inference_engine_get_result(inference_result_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memcpy(out, &s_result, sizeof(s_result));
    return ESP_OK;
}

esp_err_t inference_engine_deinit(void)
{
    if (s_arena) {
        heap_caps_free(s_arena);
        s_arena = NULL;
    }
    return ESP_OK;
}
