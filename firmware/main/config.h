#pragma once

#include "platform/platform.h"

#define KTensorArenaSize        KPlatformArenaSize      // PSRAM for TFLite
#define KSensorBufferSize       (1024 * 50)             // 50KB SRAM for PPG ring buffer
#define KBleMtuSize             512
#define KMaxOfflineEvents       100                     // 离线缓存事件数
#define KInferenceIntervalMs    40                      // 25Hz 推理频率
#define KSensorSampleRateHz     100                     // PPG 采样率
#define KPredictionHrIntervalMs 1000                    // 本地预测(HR)周期
#define KSensorPortI2c          CONFIG_SENSOR_I2C_PORT
#define KSensorI2cFreqHz        400000                  // 400kHz Fast Mode
#define KSensorSdaGpio          KPlatformI2cSda
#define KSensorSclGpio          KPlatformI2cScl
