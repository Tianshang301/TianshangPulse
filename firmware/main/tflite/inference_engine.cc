#include <string.h>
#include "tflite/inference_engine.h"
#include "sensors/signal_gate.h"
#include "af_lr_coefs.h"
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
    ESP_LOGI(TAG, "AF LR model: %d coefs, bias=%.4f, threshold=%.2f",
             K_AF_LR_N_FEATURES, (double)k_af_lr_bias, (double)k_af_lr_threshold);
    return ESP_OK;
}

esp_err_t inference_engine_run(void)
{
    memset(&s_result, 0, sizeof(s_result));
    return ESP_OK;
}

esp_err_t inference_engine_run_features(const float feat[K_AF_LR_N_FEATURES])
{
    if (!feat) return ESP_ERR_INVALID_ARG;

    memset(&s_result, 0, sizeof(s_result));
    float score = af_lr_score(feat);
    float p_af = af_lr_sigmoid(score);

    s_result.anomaly_flag = (p_af >= k_af_lr_threshold) ? 1 : 0;
    uint8_t conf = (uint8_t)(p_af * 100.0f);
    s_result.confidence = conf > 100 ? 100 : conf;
    return ESP_OK;
}

esp_err_t inference_engine_run_gated(int gated_level)
{
    memset(&s_result, 0, sizeof(s_result));

    switch (gated_level) {
    case SIGNAL_GATE_HIGH_MOTION:
    case SIGNAL_GATE_LOW_SQI:
        /* 信号不可信：跳过分类，抑制 AF 异常输出；置信度置 0 */
        s_result.confidence = 0;
        s_result.anomaly_flag = 0;
        return ESP_OK;
    case SIGNAL_GATE_LOW_MOTION:
        /* 中运动：正常推理，但压低置信度上限（30-60 区间） */
        inference_engine_run();
        if (s_result.confidence > 60) s_result.confidence = 60;
        break;
    case SIGNAL_GATE_ACTIVE:
    default:
        inference_engine_run();
        break;
    }
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