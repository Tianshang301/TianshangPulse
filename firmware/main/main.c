#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "nvs_flash.h"
#include "config.h"
#include "sensors/sensor.h"
#include "sensors/mpu6886.h"
#include "sensors/signal_gate.h"
#include "sensors/af_features.h"
#include "ble/gatt_server.h"
#include "power/power_manager.h"
#include "ui/ui_manager.h"
#include "tflite/inference_engine.h"

#define MAIN_TAG "main"

static const char *kModelTag = "model:v1.0.0";

/* 4s @100Hz 窗口（含 1 个采样点余量，环形写指针缓存） */
#define WIN_LEN             400
#define IMU_LEN             40        // 4s @10Hz 加速度样本

static float s_ppg_win[WIN_LEN];      // 当前推理窗（归一化）
static int16_t s_ax[IMU_LEN], s_ay[IMU_LEN], s_az[IMU_LEN];

static volatile bool s_win_ready = false;

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

    ESP_LOGI(MAIN_TAG, "TianshangPulse boot, %s, %s, PSRAM=%u bytes",
             kModelTag, KPlatformName, (unsigned)esp_psram_get_size());

    ESP_ERROR_CHECK(power_manager_init());
    ESP_ERROR_CHECK(sensor_init());

    esp_err_t inf_ret = inference_engine_init();
    ESP_LOGI(MAIN_TAG, "inference_engine_init -> %s", esp_err_to_name(inf_ret));

    ESP_ERROR_CHECK(ble_gatt_server_init());
    ESP_ERROR_CHECK(ui_init());

    xTaskCreate(sensor_task, "sensor", KSensorTaskStackBytes, NULL, 6, NULL);
    xTaskCreate(inference_task, "inference", KInferenceTaskStackBytes, NULL, 5, NULL);
}

static void sensor_task(void *arg)
{
    (void)arg;
    uint32_t idx = 0;
    for (;;) {
        sensor_ppg_data_t ppg = {0};
        sensor_imu_data_t imu = {0};
        sensor_sample();
        sensor_get_ppg(&ppg);
        sensor_get_imu(&imu);

        s_ppg_win[idx] = (float)ppg.heart_rate_bpm;   // 占位：当前驱动无真实波形，后续接 MAX30102 FIFO

        /* 按 10Hz 收集 IMU（每 10 次采样@100Hz 一次），4s 共 40 点 */
        if ((idx % 10) == 0) {
            uint32_t wi = idx / 10;
            if (wi < IMU_LEN) {
                s_ax[wi] = imu.accel_x;
                s_ay[wi] = imu.accel_y;
                s_az[wi] = imu.accel_z;
            }
        }

        idx = (idx + 1) % WIN_LEN;
        if (idx == 0) {
            s_win_ready = true;                       // 每满 4s 置位
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / KSensorSampleRateHz));
    }
}

static void inference_task(void *arg)
{
    (void)arg;
    signal_gate_init();
    for (;;) {
        if (s_win_ready) {
            float energy = signal_gate_motion_energy(s_ax, s_ay, s_az, IMU_LEN, 10);
            int n_peaks = 0;
            float sqi = signal_gate_ppg_sqi(s_ppg_win, WIN_LEN, KSensorSampleRateHz, &n_peaks);
            signal_gate_result_t gate = signal_gate_evaluate(energy, sqi);

            if (gate.level == SIGNAL_GATE_ACTIVE ||
                gate.level == SIGNAL_GATE_LOW_MOTION) {
                /* 信号可信：提取 6 维 RRI 特征并运行 7 参数 LR */
                float feat[6];
                int n_beat = af_features_extract(s_ppg_win, WIN_LEN,
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

            ESP_LOGI(MAIN_TAG, "gate=%d motion=%.4f sqi=%.3f peaks=%d anomaly=%u conf=%u",
                     gate.level, energy, sqi, n_peaks, res.anomaly_flag, res.confidence);
            s_win_ready = false;
        }
        vTaskDelay(pdMS_TO_TICKS(KInferenceIntervalMs));
    }
}