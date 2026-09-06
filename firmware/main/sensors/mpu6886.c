#include "sensors/mpu6886.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "config.h"

#define TAG "mpu6886"

#define MPU6886_REG_ACCEL_XOUT_H  0x3B
#define MPU6886_REG_ACCEL_RANGE   0x1C       // ACCEL_CONFIG
#define MPU6886_REG_PWR_MGMT_1    0x6B
#define MPU6886_REG_WHO_AM_I      0x75

#define MPU6886_ACCEL_RANGE_8G    0x10       // +-8g, sens 4096 LSB/g

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static float s_accel_scale = 1.0f / 4096.0f; // g/LSB @ +-8g

esp_err_t mpu6886_init(void)
{
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
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU6886_I2C_ADDR,
        .scl_speed_hz = KSensorI2cFreqHz,
    };
    ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "dev add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 唤醒 + 加速度量程 +-8g
    uint8_t pwr = 0x00;
    uint8_t accel = MPU6886_ACCEL_RANGE_8G;
    ret = i2c_master_transmit(s_dev, (uint8_t[]){MPU6886_REG_PWR_MGMT_1, pwr}, 2, -1);
    if (ret != ESP_OK) { ESP_LOGW(TAG, "pwr mgmt write failed: %s", esp_err_to_name(ret)); }
    ret = i2c_master_transmit(s_dev, (uint8_t[]){MPU6886_REG_ACCEL_RANGE, accel}, 2, -1);
    if (ret != ESP_OK) { ESP_LOGW(TAG, "accel range write failed: %s", esp_err_to_name(ret)); }
    return ESP_OK;
}

esp_err_t mpu6886_read_imu(sensor_imu_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    uint8_t reg = MPU6886_REG_ACCEL_XOUT_H;
    uint8_t buf[6] = {0};
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, buf, 6, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "accel read failed: %s", esp_err_to_name(ret));
        out->accel_x = out->accel_y = out->accel_z = 0;
        out->gyro_x = out->gyro_y = out->gyro_z = 0;
        return ESP_OK;              // 读失败置零，不阻断上层
    }
    int16_t ax = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t ay = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t az = (int16_t)((buf[4] << 8) | buf[5]);
    // 保存原始 int16（sensor_imu_data_t 类型），单位 g 换算见 mpu6886_accel_g()
    out->accel_x = ax;
    out->accel_y = ay;
    out->accel_z = az;
    out->gyro_x = out->gyro_y = out->gyro_z = 0;   // 陀螺仪暂未使能
    return ESP_OK;
}

float mpu6886_accel_g(int16_t raw)
{
    return (float)raw * s_accel_scale;
}

esp_err_t mpu6886_deinit(void)
{
    if (s_bus) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        s_dev = NULL;
    }
    return ESP_OK;
}
