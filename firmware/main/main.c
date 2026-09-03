#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "nvs_flash.h"
#include "config.h"
#include "sensors/sensor.h"
#include "ble/gatt_server.h"
#include "power/power_manager.h"
#include "ui/ui_manager.h"
#include "tflite/inference_engine.h"

#define MAIN_TAG "main"

static const char *kModelTag = "model:v1.0.0";

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

    ESP_LOGI(MAIN_TAG, "TianshangPulse boot, %s, PSRAM=%u bytes",
             kModelTag,
             (unsigned)esp_psram_get_size());

    ESP_ERROR_CHECK(power_manager_init());
    ESP_ERROR_CHECK(sensor_init());

    esp_err_t inf_ret = inference_engine_init();
    ESP_LOGI(MAIN_TAG, "inference_engine_init -> %s", esp_err_to_name(inf_ret));

    ESP_ERROR_CHECK(ble_gatt_server_init());
    ESP_ERROR_CHECK(ui_init());

    xTaskCreate(sensor_task, "sensor", 4096, NULL, 6, NULL);
    xTaskCreate(inference_task, "inference", 8192, NULL, 5, NULL);
}

static void sensor_task(void *arg)
{
    (void)arg;
    for (;;) {
        sensor_sample();
        vTaskDelay(pdMS_TO_TICKS(1000 / KSensorSampleRateHz));
    }
}

static void inference_task(void *arg)
{
    (void)arg;
    for (;;) {
        inference_engine_run();
        vTaskDelay(pdMS_TO_TICKS(KInferenceIntervalMs));
    }
}
