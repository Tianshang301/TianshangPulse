#include "sensors/max30102.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "config.h"

#define TAG "max30102"

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

esp_err_t max30102_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = KSensorPortI2c,
        .sda_io_num = CONFIG_SENSOR_SDA_GPIO,
        .scl_io_num = CONFIG_SENSOR_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bus create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30102_I2C_ADDR,
        .scl_speed_hz = KSensorI2cFreqHz,
    };
    ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "dev add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t max30102_read_ppg(sensor_ppg_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    out->heart_rate_bpm = 0;
    out->blood_oxygen_pct = 0;
    return ESP_OK;
}

esp_err_t max30102_deinit(void)
{
    if (s_bus) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        s_dev = NULL;
    }
    return ESP_OK;
}
