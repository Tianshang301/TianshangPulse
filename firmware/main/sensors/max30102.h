#pragma once

#include "esp_err.h"
#include "sensors/sensor.h"

#define MAX30102_I2C_ADDR         0x57
#define MAX30102_EXPECTED_PART_ID 0x15

/* 寄存器（MAX30102 datasheet） */
#define MAX30102_REG_FIFO_WR_PTR  0x04
#define MAX30102_REG_OVF_COUNTER  0x05
#define MAX30102_REG_FIFO_RD_PTR  0x06
#define MAX30102_REG_FIFO_DATA    0x07
#define MAX30102_REG_FIFO_CFG     0x08
#define MAX30102_REG_MODE_CFG     0x09
#define MAX30102_REG_SPO2_CFG     0x0A
#define MAX30102_REG_LED1_PA      0x0C   // IR
#define MAX30102_REG_LED2_PA      0x0D   // Red
#define MAX30102_REG_PART_ID      0xFF

/* 模式 */
#define MAX30102_MODE_SPO2        0x03   // IR + Red 双 LED
#define MAX30102_SPO2_CFG_100SPS  0x20   // SMPRATE=100, PW=69us, ADCRGE=2048nA

esp_err_t max30102_init(void);
/* 读取一帧：raw_ir 填充最新 IR 样本（归一化浮点）。
 * 模拟模式（CONFIG_SENSOR_SIM_PPG）注入合成波形，跳过硬件访问。 */
esp_err_t max30102_read_ppg(sensor_ppg_data_t *out);
esp_err_t max30102_deinit(void);