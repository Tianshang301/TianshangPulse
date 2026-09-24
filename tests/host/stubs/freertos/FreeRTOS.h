#pragma once
/* host 测试桩：FreeRTOS（单线程 host，临界区退化为空操作；
 * 仅用于验证 offline_cache 的纯逻辑语义，非并发行为） */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))