#include "sensors/mpu6886.h"
#include "sensors/sensor_i2c.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#define TAG "mpu6886"

#define MPU6886_REG_ACCEL_XOUT_H  0x3B
#define MPU6886_REG_GYRO_CONFIG   0x1B       // GYRO_CONFIG
#define MPU6886_REG_ACCEL_RANGE   0x1C       // ACCEL_CONFIG
#define MPU6886_REG_PWR_MGMT_1    0x6B
#define MPU6886_REG_WHO_AM_I      0x75

#define MPU6886_ACCEL_RANGE_8G    0x10       // +-8g, sens 4096 LSB/g
#define MPU6886_GYRO_RANGE_2000DPS 0x18      // +-2000dps, sens 16.4 LSB/(dps)

static i2c_master_dev_handle_t s_dev = NULL;
static float s_accel_scale = 1.0f / 4096.0f; // g/LSB @ +-8g
static float s_gyro_scale = 1.0f / 16.4f;    // dps/LSB @ +-2000dps

esp_err_t mpu6886_init(void)
{
    esp_err_t ret = sensor_i2c_add_device(MPU6886_I2C_ADDR, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "dev add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 唤醒 + 加速度量程 +-8g + 陀螺仪量程 +-2000dps
    uint8_t pwr = 0x00;
    uint8_t accel = MPU6886_ACCEL_RANGE_8G;
    uint8_t gyro = MPU6886_GYRO_RANGE_2000DPS;
    ret = i2c_master_transmit(s_dev, (uint8_t[]){MPU6886_REG_PWR_MGMT_1, pwr}, 2, -1);
    if (ret != ESP_OK) { ESP_LOGW(TAG, "pwr mgmt write failed: %s", esp_err_to_name(ret)); }
    ret = i2c_master_transmit(s_dev, (uint8_t[]){MPU6886_REG_ACCEL_RANGE, accel}, 2, -1);
    if (ret != ESP_OK) { ESP_LOGW(TAG, "accel range write failed: %s", esp_err_to_name(ret)); }
    ret = i2c_master_transmit(s_dev, (uint8_t[]){MPU6886_REG_GYRO_CONFIG, gyro}, 2, -1);
    if (ret != ESP_OK) { ESP_LOGW(TAG, "gyro range write failed: %s", esp_err_to_name(ret)); }
    return ESP_OK;
}

esp_err_t mpu6886_read_imu(sensor_imu_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_dev) return ESP_ERR_INVALID_STATE;   // 总线未初始化（模拟模式/无传感器）
    uint8_t reg = MPU6886_REG_ACCEL_XOUT_H;
    /* 一次读 14 字节：accel(6) + temp(2) + gyro(6)，从 0x3B 连续读 */
    uint8_t buf[14] = {0};
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, buf, sizeof(buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "imu read failed: %s", esp_err_to_name(ret));
        out->accel_x = out->accel_y = out->accel_z = 0;
        out->gyro_x = out->gyro_y = out->gyro_z = 0;
        return ESP_OK;              // 读失败置零，不阻断上层
    }
    int16_t ax = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t ay = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t az = (int16_t)((buf[4] << 8) | buf[5]);
    int16_t gx = (int16_t)((buf[8] << 8) | buf[9]);
    int16_t gy = (int16_t)((buf[10] << 8) | buf[11]);
    int16_t gz = (int16_t)((buf[12] << 8) | buf[13]);
    // 保存原始 int16（sensor_imu_data_t 类型），单位换算见 mpu6886_accel_g()/mpu6886_gyro_dps()
    out->accel_x = ax;
    out->accel_y = ay;
    out->accel_z = az;
    out->gyro_x = gx;
    out->gyro_y = gy;
    out->gyro_z = gz;
    return ESP_OK;
}

float mpu6886_accel_g(int16_t raw)
{
    return (float)raw * s_accel_scale;
}

float mpu6886_gyro_dps(int16_t raw)
{
    return (float)raw * s_gyro_scale;
}

esp_err_t mpu6886_deinit(void)
{
    s_dev = NULL;   // 共享总线由 sensor_i2c_deinit() 统一销毁
    return ESP_OK;
}
