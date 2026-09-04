# TianshangPulse 中文文档

> **基于 ESP32-P4 的端侧 AI 智能手表固件**，与 [TianshangHealth](https://github.com/Tianshang301/TianshangHealth) Android APP 构成完全离线的健康监测生态。

- **English**: [README.md](../README.md)

## 核心能力

- **端侧 AI**：750K 参数 INT8 LSTM 实时推理，延迟 <10ms，零云端依赖
- **传感器**：MAX30102（PPG 心率/血氧）+ MPU6886（加速度/陀螺仪）
- **通信**：BLE GATT Server —— 实时心率/血氧/异常事件通知，离线事件缓存与批量上报
- **界面**：LVGL，可定制主题

## 技术栈

| 类别 | 技术 |
|------|------|
| 平台 | ESP32-P4 (400MHz 双核 RISC-V, 16MB PSRAM) |
| 框架 | ESP-IDF v5.4+ (C/C++) + FreeRTOS |
| 推理 | TensorFlow Lite Micro (`esp-tflite-micro`) |
| 界面 | LVGL v9 |
| 通信 | NimBLE GATT Server |

## 目录结构

```
firmware/                     # ESP-IDF 项目
├── main/
│   ├── config.h              # 关键常量（arena / 缓冲 / MTU / 采样率）
│   ├── main.c                # 入口 + sensor/inference 任务
│   ├── tflite/               # 推理引擎（init/run/deinit，PSRAM arena）
│   ├── sensors/              # 传感器统一接口 + MAX30102 + MPU6886
│   ├── ble/                  # GATT Server + 离线事件缓存
│   ├── ui/                   # LVGL 界面
│   └── power/                # 功耗管理（active / light-sleep / standby）
├── sdkconfig.defaults        # 目标芯片 / PSRAM / BLE / I2C 默认配置
└── main/idf_component.yml    # 组件依赖（lvgl, esp-tflite-micro）
```

其他目录：`docs/`（协议 / 硬件 / 功耗 / 模型 / 内存布局）、`scripts/`（模型转换与验证）、`model/`、`data/`、`hardware/`

## 构建

### 前置条件

- [ESP-IDF v5.4](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/)
- 芯片：ESP32-P4（需 16MB PSRAM 型号）

### 首次构建

```bash
# 加载 ESP-IDF 环境（PowerShell）
. $env:IDF_PATH\export.ps1

cd firmware
idf.py set-target esp32p4
idf.py build
```

首次构建时 ESP Component Manager 会自动拉取 `lvgl` 与 `esp-tflite-micro` 组件。

### 烧录与监控

```bash
idf.py flash monitor
```

## 内存策略

- 内部 SRAM (768KB)：热路径数据、PPG 缓冲、任务栈
- PSRAM (16MB)：TFLite arena（1MB）、模型权重、离线事件缓存
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
- `docs/MEMORY_LAYOUT.md` — 内存布局
- `docs/POWER_BUDGET.md` — 功耗预算
- `AGENTS.md` — AI 辅助开发行为规范

## 许可证

MIT License