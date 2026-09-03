# TianshangPulse 内存布局

## 1. 内存资源

| 区域 | 大小 | 用途 |
|------|------|------|
| 内部 SRAM | 768KB | 热路径数据、任务栈、PPG 环形缓冲 |
| PSRAM | 16MB | TFLite arena、模型权重、离线缓存 |

## 2. 分配策略

| 数据 | 区域 | 大小 | 说明 |
|------|------|------|------|
| TFLite arena | PSRAM | 1MB（`KTensorArenaSize`） | `heap_caps_malloc(MALLOC_CAP_SPIRAM)` |
| PPG 环形缓冲 | SRAM | 50KB（`KSensorBufferSize`） | 采样热路径 |
| 离线事件缓存 | PSRAM | 100 条 | 循环缓冲区 |
| 模型权重 | PSRAM | 待定 | 由 arena 承载 |

## 3. 分配规则

- **禁止**在 ISR 中调用 `malloc`/`free`
- TFLite arena 必须通过 `heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` 分配
- 内部 SRAM 优先分配给热路径（传感器采样、任务栈）
- 大块数据（模型、缓存）一律放 PSRAM

## 4. FreeRTOS 任务栈

| 任务 | 栈大小 | 优先级 |
|------|--------|--------|
| sensor | 4096 B | 6 |
| inference | 8192 B | 5 |

> 待实测：`uxTaskGetStackHighWaterMark()` 验证余量。
