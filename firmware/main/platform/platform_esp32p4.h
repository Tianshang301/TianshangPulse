#pragma once

#include <stdint.h>

#define KPlatformName              "ESP32-P4"

#define KPlatformCpuMaxMhz         (400)
#define KPlatformCpuMinMhz         (40)

#define KPlatformSramBytes         (768 * 1024)
#define KPlatformPsramBytes        (16 * 1024 * 1024)
#define KPlatformFlashSize         (16 * 1024 * 1024)

#define KPlatformArenaSize         (1024 * 1024)

/* 主任务栈（app_main）由 Kconfig 决定：CONFIG_ESP_MAIN_TASK_STACK_SIZE = 8192
 * （见 firmware/sdkconfig.defaults）。曾有的 KMainTaskStackBytes(4096) 常量
 * 从未被引用且与 Kconfig 冲突，已移除，避免出现第二个"真相源"。 */
#define KSensorTaskStackBytes      (4096)
#define KInferenceTaskStackBytes   (8192)

#define KPlatformI2cSda            (18)
#define KPlatformI2cScl            (8)
