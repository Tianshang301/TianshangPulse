#pragma once
/* host 测试桩：esp_log.h（I 级静默，E/W 到 stderr） */
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) ((void)(tag))
#define ESP_LOGW(tag, fmt, ...) do { fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define ESP_LOGE(tag, fmt, ...) do { fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__); } while (0)