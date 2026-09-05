#pragma once

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4
    #include "platform_esp32p4.h"
#elif CONFIG_IDF_TARGET_ESP32S3
    #include "platform_esp32s3.h"
#else
    #error "Unsupported target: TianshangPulse requires ESP32-P4 or ESP32-S3"
#endif
