#pragma once

#include "platform/platform.h"

#define KTensorArenaSize        KPlatformArenaSize      // PSRAM for TFLite
#define KSensorBufferSize       (1024 * 50)             // 50KB SRAM for PPG ring buffer
#define KBleMtuSize             512
#define KMaxOfflineEvents       100                     // 离线缓存事件数
#define KAdvDeviceName          "TianshangPulse"        // BLE 设备名（GAP + 广播共用，单一真源）
#define KAdvFastBurstMs         30000                   // 广播 FAST 起播后切 SLOW 的时长（P6）
#define KSensorSampleRateHz     100                     // PPG 采样率（与 MAX30102 100sps FIFO 对齐）
#define KInferenceWaitTimeoutMs 10000                   // 推理等待新窗超时（正常 4s/窗；超时兜底喂狗）
#define KPowerStandbyWakeSec    600                     // 待机深度睡眠唤醒周期（10 分钟）
#define KSensorPortI2c          CONFIG_SENSOR_I2C_PORT
#define KSensorI2cFreqHz        400000                  // 400kHz Fast Mode
#define KSensorSdaGpio          KPlatformI2cSda
#define KSensorSclGpio          KPlatformI2cScl