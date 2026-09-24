#pragma once
/* host 测试桩：esp_heap_caps.h（PSRAM 分配退化为普通 malloc） */
#include <stdlib.h>
#define MALLOC_CAP_8BIT (1 << 7)
#define MALLOC_CAP_SPIRAM (1 << 10)
static inline void *heap_caps_malloc(size_t size, int caps) { (void)caps; return malloc(size); }
static inline void heap_caps_free(void *p) { free(p); }