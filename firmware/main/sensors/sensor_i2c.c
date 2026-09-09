#include "sensors/sensor_i2c.h"
#include "esp_log.h"
#include "config.h"

#define TAG "sensor_i2c"

static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t sensor_i2c_init(void)
{
    if (s_bus) {
        return ESP_OK;   // 幂等：已创建则直接复用
    }
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = KSensorPortI2c,
        .sda_io_num = KSensorSdaGpio,
        .scl_io_num = KSensorSclGpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bus create failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t sensor_i2c_add_device(uint8_t addr, i2c_master_dev_handle_t *out)
{
    if (!s_bus || !out) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = KSensorI2cFreqHz,
    };
    return i2c_master_bus_add_device(s_bus, &dev_cfg, out);
}

esp_err_t sensor_i2c_deinit(void)
{
    if (s_bus) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    return ESP_OK;
}
