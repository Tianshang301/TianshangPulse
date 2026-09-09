#include <string.h>
#include "esp_log.h"
#include "sensors/sensor.h"
#include "sensors/sensor_i2c.h"
#include "sensors/max30102.h"
#include "sensors/mpu6886.h"

#define SENSOR_TAG "sensor"

static sensor_ppg_data_t s_ppg = {0};
static sensor_imu_data_t s_imu = {0};

esp_err_t sensor_init(void)
{
    esp_err_t ret = sensor_i2c_init();   // 共享 I2C 总线（MAX30102 + MPU6886 挂载）
    if (ret != ESP_OK) {
        ESP_LOGW(SENSOR_TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
    }
    ret = max30102_init();
    if (ret != ESP_OK) {
        ESP_LOGW(SENSOR_TAG, "MAX30102 init failed: %s", esp_err_to_name(ret));
    }
    ret = mpu6886_init();
    if (ret != ESP_OK) {
        ESP_LOGW(SENSOR_TAG, "MPU6886 init failed: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

esp_err_t sensor_sample(void)
{
    max30102_read_ppg(&s_ppg);
    mpu6886_read_imu(&s_imu);
    return ESP_OK;
}

esp_err_t sensor_get_ppg(sensor_ppg_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memcpy(out, &s_ppg, sizeof(s_ppg));
    return ESP_OK;
}

esp_err_t sensor_get_imu(sensor_imu_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memcpy(out, &s_imu, sizeof(s_imu));
    return ESP_OK;
}

esp_err_t sensor_deinit(void)
{
    mpu6886_deinit();
    max30102_deinit();
    sensor_i2c_deinit();
    return ESP_OK;
}
