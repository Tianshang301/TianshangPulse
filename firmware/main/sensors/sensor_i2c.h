#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

/* 共享 I2C 总线：MAX30102 与 MPU6886 共用同一端口。
 * 总线由本模块统一创建/销毁，传感器驱动通过 sensor_i2c_add_device() 挂载。 */

esp_err_t sensor_i2c_init(void);
esp_err_t sensor_i2c_add_device(uint8_t addr, i2c_master_dev_handle_t *out);
esp_err_t sensor_i2c_deinit(void);
