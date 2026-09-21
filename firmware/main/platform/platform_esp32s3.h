#pragma once

#include <stdint.h>

#define KPlatformName              "ESP32-S3"

#define KPlatformCpuMaxMhz         (240)
#define KPlatformCpuMinMhz         (80)

#define KPlatformSramBytes         (512 * 1024)
#define KPlatformPsramBytes        (8 * 1024 * 1024)
#define KPlatformFlashSize         (16 * 1024 * 1024)

#define KPlatformArenaSize         (1024 * 1024)

#define KMainTaskStackBytes        (4096)
#define KSensorTaskStackBytes      (3072)
#define KInferenceTaskStackBytes   (8192)

#define KPlatformI2cSda            (18)
#define KPlatformI2cScl            (8)
