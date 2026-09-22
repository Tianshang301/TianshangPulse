# TianshangPulse 功耗预算

## 1. 目标

| 模式 | 目标电流 |
|------|----------|
| 主动监测模式 | ≤ 30mA |
| 待机模式 | ≤ 500μA |

## 2. 电源域估算

### ESP32-P4（RISC-V）

| 模块 | 工作电流 | 说明 |
|------|----------|------|
| HP 核（400MHz 推理） | ~60mA | 仅推理时全速（`KPlatformCpuMaxMhz=400`） |
| LP 核（40MHz） | ~3mA | 看门狗 + 中断唤醒 |
| PSRAM | 半休眠 | 空闲时降功耗 |
| MAX30102（单次采样） | ~0.7mA | One-shot 模式 |
| MPU6500 / MPU6886 | ~1mA | 加速度+陀螺仪（±8g/±2000dps）；MPU6500 为替代件 |
| 屏幕 | 未实测 | ILI9341 2.8" SPI + 背光常开（⏳ 待上板实测） |
| BLE 广播/连接 | ~10mA 峰值 | 周期性广播 |

### ESP32-S3（Xtensa，N16R8）

| 模块 | 工作电流 | 说明 |
|------|----------|------|
| 双核（240MHz 推理） | ~40mA | `esp_pm_configure` 上限 `KPlatformCpuMaxMhz=240` |
| 空闲降频 | ~80MHz | `KPlatformCpuMinMhz=80`（S3 无独立 LP 核） |
| PSRAM | 半休眠 | 空闲时降功耗 |
| MAX30102 / MPU6500 | 同 P4 | 相同传感器方案（MPU6500 替代 MPU6886） |
| 屏幕 | 未实测 | ILI9341 2.8" SPI + 背光常开（⏳ 待上板实测） |
| BLE 广播/连接 | ~10mA 峰值 | 周期性广播 |

> CPU 频率由 `power/power_manager.c` 读取 `KPlatformCpuMaxMhz` / `KPlatformCpuMinMhz`（platform 头文件）参数化配置。

## 3. 低功耗策略（checklist）

- [ ] P4：LP 核（40MHz）负责看门狗和简单中断唤醒
- [x] HP 核（最大频率）仅在连接/推理时全速运行，断开后降频+浅睡；S3 由 `esp_pm` 自动调频
- [ ] PSRAM 在空闲时进入半休眠模式
- [ ] 传感器使用单次采样模式（One-shot），禁止连续高功耗模式
- [ ] 推理频率 25Hz，每次 <10ms，占空比 ~25%
- [ ] 屏幕背光 PWM 调光（当前为常开 GPIO，⏳ 依赖上板实测功耗数据后再定策略）
- [x] **IMU 信号质量门控**（外部验证结论，见 `docs/BENCHMARK.md` §4-5）：
      腕部运动时纯特征分类器 AF 误报 0.79–0.93（vs 安静 0.16），实测门控后降至 0.037。
      已实现：`mpu6886` 实读 + `signal_gate.c` 运动能量（0.5Hz 高通方差）+ PPG SQI 分级。
      成本：每窗 1 次标量能量计算 + SQI（~400 次峰判定），<0.1ms，可忽略

## 3.1 电源模式状态机（`power/power_manager.c`）

| 模式 | 锁状态 | CPU | Light Sleep | 触发 |
|------|--------|-----|-------------|------|
| **ACTIVE** | 持有 `CPU_FREQ_MAX` | 全速 | 禁止 | BLE 连接 / 启动默认 |
| **LIGHT_SLEEP** | 释放锁 | auto-DFS | 允许（间隙） | BLE 断开 |
| **STANDBY** | — | 深度睡眠 | 唤醒即复位 | API 调用（定时器 `KPowerStandbyWakeSec`=600s） |

转换链：`ACTIVE ⇄ LIGHT_SLEEP`（BLE 连接/断开驱动）；`→ STANDBY`（待调用 API）。
`esp_pm_configure()` 启用 auto-DFS + light sleep；`esp_pm_lock` 控制全局粒度。
auto-DFS 在 `vTaskDelay` 间隙自动降频，无需推理级细粒度锁（LR 7 参数 <1ms）。

## 4. 详细测量

> 待实测：各模式下的具体电流数据（S3 上板后补充）。