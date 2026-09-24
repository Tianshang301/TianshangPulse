#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "config.h"
#include "sensors/sensor.h"
#include "sensors/mpu6886.h"
#include "sensors/signal_gate.h"
#include "sensors/af_features.h"
#include "sensors/ppg_preprocess.h"
#include "ble/gatt_server.h"
#include "ble/offline_cache.h"
#include "power/power_manager.h"
#include "ui/ui_manager.h"
#include "tflite/inference_engine.h"

#define MAIN_TAG "main"

static const char *kModelTag = "model:v1.0.0";

/* 4s @100Hz 窗口；双缓冲：sensor 写 fill 侧，每满一窗发布给 inference */
#define WIN_LEN             400
#define IMU_LEN             40        // 4s @10Hz 加速度样本

static float s_ppg_win[2][WIN_LEN];
static int16_t s_ax[2][IMU_LEN], s_ay[2][IMU_LEN], s_az[2][IMU_LEN];

/* 发布槽：-1=无就绪窗；0/1=已发布待消费的缓冲下标。
 * 生产者（sensor task）发布、消费者（inference task）清除，均持短临界区。
 * 消费者忙时丢弃新窗（新数据覆写 fill 侧），保持 4s 非重叠训练语义。 */
static portMUX_TYPE s_win_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int s_ready_idx = -1;

/* 归一化推理窗（inference task 私有，放静态区省 1.6KB 任务栈） */
static float s_norm[WIN_LEN];

static TaskHandle_t s_inference_task_handle;

static void sensor_task(void *arg);
static void inference_task(void *arg);

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

#if CONFIG_SPIRAM
    ESP_LOGI(MAIN_TAG, "TianshangPulse boot, %s, %s, PSRAM=%u bytes",
             kModelTag, KPlatformName, (unsigned)esp_psram_get_size());
#else
    ESP_LOGI(MAIN_TAG, "TianshangPulse boot, %s, %s, PSRAM disabled (no SPIRAM)",
             kModelTag, KPlatformName);
#endif

    ESP_ERROR_CHECK(power_manager_init());
    /* sensor_init 现为容错（sensor.c 各子传感器失败仅 warning，末尾无条件 ESP_OK），
     * 这里改为不 abort：即便将来 sensor_init 真返错误，BLE 仍要起得来（手表可连、APP 可
     * 显示「传感器故障」）。功能性降级不阻断连接与广播。 */
    esp_err_t sen_ret = sensor_init();
    ESP_LOGW(MAIN_TAG, "sensor_init -> %s", esp_err_to_name(sen_ret));

    esp_err_t inf_ret = inference_engine_init();
    ESP_LOGI(MAIN_TAG, "inference_engine_init -> %s", esp_err_to_name(inf_ret));

    ESP_ERROR_CHECK(ble_gatt_server_init());
    ESP_ERROR_CHECK(offline_cache_init());
    ESP_ERROR_CHECK(ui_init());

    /* 显示初始化 + LVGL 控件创建在本任务上同步完成（栈需求见
     * sdkconfig.defaults 的 CONFIG_ESP_MAIN_TASK_STACK_SIZE 说明）。
     * 上板后据此日志校验余量：HWM 明显偏小则再提高主任务栈。 */
    ESP_LOGI(MAIN_TAG, "main stack HWM=%u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    /* 先建 inference（拿到句柄供 sensor 发布通知），再建 sensor。
     * 任务创建失败属配置级错误：直接中止。 */
    if (xTaskCreate(inference_task, "inference", KInferenceTaskStackBytes,
                    NULL, 5, &s_inference_task_handle) != pdPASS ||
        xTaskCreate(sensor_task, "sensor", KSensorTaskStackBytes,
                    NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(MAIN_TAG, "task create failed");
        return;
    }
}

static void sensor_task(void *arg)
{
    (void)arg;
    int fill = 0;                       // 当前填充缓冲下标
    uint32_t idx = 0;
    uint32_t loop = 0;
    TickType_t last_wake = xTaskGetTickCount();

    (void)esp_task_wdt_add(NULL);       // 订阅任务看门狗（TWDT 未启用则忽略）
    for (;;) {
        sensor_ppg_data_t ppg = {0};
        sensor_imu_data_t imu = {0};
        sensor_sample();
        sensor_get_ppg(&ppg);
        sensor_get_imu(&imu);

        s_ppg_win[fill][idx] = ppg.raw_ir;   // 原始 IR 波形样本（FIFO 最新帧）

        /* 按 10Hz 收集 IMU（每 10 次采样@100Hz 一次），4s 共 40 点 */
        if ((idx % 10) == 0) {
            uint32_t wi = idx / 10;
            if (wi < IMU_LEN) {
                s_ax[fill][wi] = imu.accel_x;
                s_ay[fill][wi] = imu.accel_y;
                s_az[fill][wi] = imu.accel_z;
            }
        }

        idx = (idx + 1) % WIN_LEN;
        if (idx == 0) {
            int published = -1;
            portENTER_CRITICAL(&s_win_mux);
            if (s_ready_idx < 0) {           // 消费者空闲：发布本窗并切换缓冲
                published = fill;
                s_ready_idx = fill;
                fill ^= 1;
            }
            /* 消费者忙：不发布（继续覆写 fill 缓冲），非重叠语义 */
            portEXIT_CRITICAL(&s_win_mux);
            if (published >= 0) {
                xTaskNotifyGive(s_inference_task_handle);
            }
        }

        esp_task_wdt_reset();
        /* 定周期采样：与 MAX30102 100sps FIFO 对齐，消除 vTaskDelay 漂移 */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000 / KSensorSampleRateHz));

        if ((++loop % 1500) == 0) {          // ~15s 一次栈余量自检
            ESP_LOGI(MAIN_TAG, "sensor stack HWM=%u",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

static void inference_task(void *arg)
{
    (void)arg;
    signal_gate_init();
    uint32_t win_count = 0;

    (void)esp_task_wdt_add(NULL);
    for (;;) {
        /* 事件驱动：sensor 每满 4s 窗通知一次；超时兜底喂狗后重等 */
        BaseType_t got = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(KInferenceWaitTimeoutMs));
        esp_task_wdt_reset();
        if (got == pdFALSE) {
            continue;   // 长时间无窗 -> 任务看门狗兜底复位
        }

        int idx;
        portENTER_CRITICAL(&s_win_mux);
        idx = s_ready_idx;
        portEXIT_CRITICAL(&s_win_mux);
        if (idx < 0) continue;

        /* 与训练端一致的预处理：逐窗 Z-score + clip ±3，之后 SQI/特征/HR
         * 全部基于归一化窗（与 scripts/ppg_sqi.py、train_af_model.py 对齐）。 */
        ppg_preprocess_zscore_clip(s_ppg_win[idx], s_norm, WIN_LEN);

        float energy = signal_gate_motion_energy(s_ax[idx], s_ay[idx], s_az[idx], IMU_LEN, 10);
        int n_peaks = 0;
        float sqi = signal_gate_ppg_sqi(s_norm, WIN_LEN, KSensorSampleRateHz, &n_peaks);
        signal_gate_result_t gate = signal_gate_evaluate(energy, sqi);

        int n_beat = 0;
        if (gate.level == SIGNAL_GATE_ACTIVE ||
            gate.level == SIGNAL_GATE_LOW_MOTION) {
            /* 信号可信：提取 6 维 RRI 特征并运行 7 参数 LR */
            float feat[6];
            n_beat = af_features_extract(s_norm, WIN_LEN,
                                         KSensorSampleRateHz, feat);
            if (n_beat >= 3) {
                inference_engine_run_features(feat);
            } else {
                inference_engine_run();
            }
        } else {
            inference_engine_run_gated(gate.level);
        }

        inference_result_t res = {0};
        inference_engine_get_result(&res);
        if (gate.level == SIGNAL_GATE_LOW_MOTION && res.confidence > 60) {
            res.confidence = 60;   /* 中运动压低置信度上限 */
        }

        /* 仅在安静(ACTIVE)信号可信时上报 AF 异常：运动/低质量抑制误报 */
        if (res.anomaly_flag && gate.level == SIGNAL_GATE_ACTIVE) {
            ble_anomaly_event_t ev = {
                .type = BLE_EVENT_AF_ANOMALY,
                .timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000),
                .confidence = res.confidence,
            };
            offline_cache_push(&ev);                 /* 离线兜底（未连接时积累） */
            ble_notify_anomaly(&ev);                 /* 已连接则实时推送 */
        }

        /* 周期心率上报：4s 窗内峰数 -> BPM（hr_proxy x 60），每 4s 一次 */
        if (n_beat >= 3) {
            uint16_t bpm = (uint16_t)((float)n_beat /
                            ((float)WIN_LEN / (float)KSensorSampleRateHz) * 60.0f);
            ble_notify_heart_rate(bpm);
        }

        ESP_LOGI(MAIN_TAG, "gate=%d motion=%.4f sqi=%.3f peaks=%d anomaly=%u conf=%u",
                 gate.level, energy, sqi, n_peaks, res.anomaly_flag, res.confidence);

        /* 释放本窗：仅当期间无新发布（有则留给下一轮，避免覆盖在读缓冲） */
        portENTER_CRITICAL(&s_win_mux);
        if (s_ready_idx == idx) {
            s_ready_idx = -1;
        }
        portEXIT_CRITICAL(&s_win_mux);

        if ((++win_count % 15) == 0) {           // ~1min 一次栈余量自检
            ESP_LOGI(MAIN_TAG, "inference stack HWM=%u",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
    }
}