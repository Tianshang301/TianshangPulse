# TianshangPulse 硬件设计

> 所有元器件须满足 AGENTS.md §4 约束：嘉立创可采购或淘宝常用现货。

## 1. 目标平台（二选一）

| 平台 | SoC | Flash | PSRAM | 架构 | 优先级 |
|------|-----|-------|-------|------|--------|
| **ESP32-S3** | ESP32-S3-WROOM-1-N8R8 | 8MB | 8MB OPI | Xtensa LX7 双核 240MHz | **首选**（低成本快速验证） |
| **ESP32-P4** | ESP32-P4NRW16 / NRW32 | 16MB | 16MB | RISC-V 双核 400MHz | 后续迁移 |

> 固件支持双目标，通过 `idf.py set-target esp32s3|esp32p4` 切换；目标相关配置见 `firmware/main/platform/`。

## 2. 核心芯片

| 器件 | 型号 | 说明 |
|------|------|------|
| PPG | MAX30102 | I2C，心率/血氧 |
| IMU | MPU6886 | I2C，加速度（±8g）+ 陀螺仪（±2000dps） |
| 屏幕 | （待选型） | SPI 接口 LCD/AMOLED |

## 3. 接口分配

| 外设 | 接口 | GPIO | 说明 |
|------|------|------|------|
| MAX30102 | I2C0 | SDA=18, SCL=8 | 两平台一致（`KPlatformI2cSda/Scl`） |
| MPU6886 | I2C0 | 与 MAX30102 共享总线 | |
| 屏幕 | SPI | 待定 | S3 上需避开 PSRAM/Flash 占用引脚 |
| 电池管理 | — | 待定 | |

> S3 的 Strapping 引脚为 GPIO0/3/45/46，GPIO8/18 非 Strapping，无启动冲突。
> 烧录验证前仍应核对所选 S3 板原理图，确认 GPIO8/18 未被板载外设占用。

## 4. 设计要点

- PSRAM 高速信号：阻抗控制、等长走线（P4 尤甚）
- 传感器使用 I2C 接口（减少 GPIO 占用）
- 低功耗：
  - P4：LP 核（40MHz）负责看门狗与中断唤醒，HP 核（400MHz）仅推理时全速运行
  - S3：`esp_pm` 自动调频（240/160/80MHz），无独立 LP 核
- S3 OPI PSRAM 走线：CS=GPIO26、CLK=GPIO30（SDK 默认，勿挪作他用）

## 5. BOM 状态

> 待补全：屏幕型号、电池规格、电源管理 IC。