# TianshangPulse 内存布局

## 1. 内存资源

| 平台 | 内部 SRAM | PSRAM |
|------|-----------|-------|
| ESP32-P4 | 768KB | 16MB |
| ESP32-S3 (N16R8) | 512KB（含 256KB 瓷片内部） | 8MB |

> 目标相关常量统一在 `firmware/main/platform/platform_<target>.h` 中声明，编译期由 `CONFIG_IDF_TARGET` 决定。

## 2. 分配策略

| 数据 | 区域 | 大小 | 说明 |
|------|------|------|------|
| TFLite arena | PSRAM | 1MB（`KTensorArenaSize`） | `heap_caps_malloc(MALLOC_CAP_SPIRAM)`，两平台一致 |
| PPG 环形缓冲 | SRAM | 50KB（`KSensorBufferSize`） | 采样热路径 |
| 离线事件缓存 | PSRAM | 100 条 | 循环缓冲区 |
| 模型权重 | PSRAM | 待定 | 由 arena 承载 |
| LVGL 绘制缓冲 | PSRAM | 150KB（240×320×2B） | esp_lvgl_port `buff_spiram`；小 SRAM 块流式搬运到 SPI DMA |

## 3. Flash 分区（S3，仅前缀一致、storage 大小不同）

| 分区 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| nvs | 0x9000 | 24KB | NVS |
| phy_init | 0xf000 | 4KB | PHY 校准数据 |
| factory | 0x10000 | 3MB | 应用 |
| model | 0x310000 | 1MB | 模型/资产（spiffs） |
| storage | 0x410000 | 其余全部 | 离线数据（spiffs） |

- 8MB Flash（N8R8）：`partitions_8mb.csv`，storage = 0x3F0000（~3.9MB）
- 16MB Flash（N16R8，**当前 S3 目标**）：`partitions_16mb.csv`，storage = 0xBF0000（~12MB）

## 4. 分配规则

- **禁止**在 ISR 中调用 `malloc`/`free`
- TFLite arena 必须通过 `heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` 分配
- 内部 SRAM 优先分配给热路径（传感器采样、任务栈）
- 大块数据（模型、缓存）一律放 PSRAM
- S3 通过 `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` 确保显式 `MALLOC_CAP_SPIRAM` 才用 PSRAM，普通 `malloc` 不占用

## 5. FreeRTOS 任务栈

| 任务 | P4 | S3 | 优先级 |
|------|----|----|--------|
| sensor | 4096 B | 3072 B | 6 |
| inference | 8192 B | 8192 B | 5 |

> 常量：`KSensorTaskStackBytes` / `KInferenceTaskStackBytes`（platform 头文件）。
> 待实测：`uxTaskGetStackHighWaterMark()` 验证余量，S3 若栈不足可回退至 P4 值。