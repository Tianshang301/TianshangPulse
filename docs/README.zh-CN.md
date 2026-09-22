# TianshangPulse 中文文档

> **基于 ESP32-P4 的端侧 AI 智能手表固件**，与 [TianshangHealth](https://github.com/Tianshang301/TianshangHealth) Android APP 构成完全离线的健康监测生态。

- **English**: [README.md](../README.md)

## 核心能力

- **端侧 AI**：7 参数逻辑回归（RR 间期特征，val AUC 0.928）+ 运动门控，零云端依赖
- **传感器**：MAX30102（PPG 心率/血氧）+ MPU6886（加速度/陀螺仪）
- **通信**：BLE GATT Server —— 实时心率/血氧/异常事件通知，离线事件缓存与批量上报
- **界面**：LVGL，可定制主题

## 技术栈

| 类别 | 技术 |
|------|------|
| 平台 | ESP32-P4 (400MHz 双核 RISC-V, 16MB PSRAM) |
| 框架 | ESP-IDF v5.4+ (C/C++) + FreeRTOS |
| 推理 | 逻辑回归（7 参数，硬编码）+ 运动门控；TensorFlow Lite Micro 为深度模型预留 |
| 界面 | LVGL v9 |
| 通信 | NimBLE GATT Server |

## 目录结构

```
firmware/                     # ESP-IDF 项目
├── main/
│   ├── config.h              # 关键常量（arena / 缓冲 / MTU / 采样率）
│   ├── main.c                # 入口 + sensor/inference 任务
│   ├── platform/             # 目标抽象层（ESP32-P4 / ESP32-S3 常量）
│   ├── tflite/               # 推理引擎（init/run/deinit，PSRAM arena）
│   ├── sensors/              # 传感器统一接口 + MAX30102 + MPU6886
│   ├── ble/                  # GATT Server + 离线事件缓存
│   ├── ui/                   # LVGL 界面
│   └── power/                # 功耗管理（active / light-sleep / standby）
├── sdkconfig.defaults          # 通用配置（FreeRTOS / BLE / I2C 端口）
├── sdkconfig.defaults.esp32p4  # P4：PSRAM 120M、16MB Flash
├── sdkconfig.defaults.esp32s3  # S3：OPI PSRAM 80M、16MB Flash、自定义分区表
├── partitions_8mb.csv          # 自定义 8MB 分区表（esp32s3，旧 N8R8）
├── partitions_16mb.csv          # 自定义 16MB 分区表（esp32s3，N16R8）

└── main/idf_component.yml      # 组件依赖（lvgl, esp-tflite-micro, esp_lvgl_port, esp_lcd_ili9341）
```

其他目录：`docs/`（协议 / 硬件 / 功耗 / 模型 / 内存布局）、`scripts/`（模型转换与验证）、`model/`、`data/`、`hardware/`

硬件到货前准备的交接状态（16MB Flash + ILI9341 显示驱动，仅完成主机侧/编译验证）：[`docs/SYNC_HARDWARE_PREP.md`](./SYNC_HARDWARE_PREP.md)

## 构建

### 前置条件

- [ESP-IDF v5.4](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/)
- 目标芯片（二选一）：
  - **ESP32-P4**（16MB Flash + 16MB PSRAM）
  - **ESP32-S3 N16R8**（16MB Flash + 8MB OPI PSRAM）——推荐先从此芯片开始

### 首次构建

```bash
# 加载 ESP-IDF 环境（PowerShell）
. $env:IDF_PATH\export.ps1

cd firmware

# ESP32-S3（默认开发目标）
idf.py set-target esp32s3

# 或 ESP32-P4
# idf.py set-target esp32p4

idf.py build
```

首次构建时 ESP Component Manager 会自动拉取 `lvgl`、`esp-tflite-micro`、`esp_lvgl_port` 与 `esp_lcd_ili9341` 组件。

SDK 会自动加载对应的 `sdkconfig.defaults.<目标芯片>`。目标相关常量统一放在 `firmware/main/platform/`。

### 烧录与监控

```bash
idf.py flash monitor
```

> 注意：`esp32s3`（N16R8）使用自定义 16MB 分区表（`partitions_16mb.csv`）。
> 旧的 `partitions_8mb.csv` 保留给 N8R8 板。
> 切换目标时，如遇到 target 不匹配错误，先 `idf.py fullclean` 并删除 `sdkconfig`。

## 内存策略

- 内部 SRAM：热路径数据、PPG 缓冲、任务栈（P4 768KB / S3 512KB）
- PSRAM：TFLite arena（1MB）、模型权重、离线事件缓存、LVGL 绘制缓冲（P4 16MB / S3 8MB）
- 详细布局：`docs/MEMORY_LAYOUT.md`

## 功耗目标

| 模式 | 目标电流 |
|------|----------|
| 主动监测 | ≤ 30mA |
| 待机 | ≤ 500μA |

详细预算：`docs/POWER_BUDGET.md`

## 文档

- `README.md` — English documentation
- `docs/PROTOCOL.md` — BLE GATT 协议（Source of Truth）
- `docs/HARDWARE.md` — 硬件设计与引脚分配
- `docs/MODEL_ARCH.md` — 模型架构与量化流水线
- `docs/BENCHMARK.md` — 外部验证基准（峰值检测 / 运动伪影鲁棒性）
- `docs/MEMORY_LAYOUT.md` — 内存布局
- `docs/POWER_BUDGET.md` — 功耗预算
- `AGENTS.md` — AI 辅助开发行为规范

## 许可证

MIT License