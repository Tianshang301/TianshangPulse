#pragma once

#include <stdint.h>

#define KPlatformName              "ESP32-P4"

#define KPlatformCpuMaxMhz         (400)
#define KPlatformCpuMinMhz         (40)

#define KPlatformSramBytes         (768 * 1024)
#define KPlatformPsramBytes        (16 * 1024 * 1024)
#define KPlatformFlashSize         (16 * 1024 * 1024)

#define KPlatformArenaSize         (1024 * 1024)

#define KMainTaskStackBytes        (4096)
#define KSensorTaskStackBytes      (4096)
#define KInferenceTaskStackBytes   (8192)

#define KPlatformI2cSda            (18)
#define KPlatformI2cScl            (8)
