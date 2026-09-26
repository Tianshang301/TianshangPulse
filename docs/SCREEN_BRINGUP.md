# 屏幕点亮与显示调试开发文档（ILI9341 2.8" / ESP32-S3）

**日期**：2026-09-24 ~ 2026-09-26
**验证硬件**：ESP32-S3-WROOM-1-1U64-N16R8 开发板（MAC `84:c7:bb:70:5d:a4`），ILI9341 2.8 寸 SPI 彩屏（带触摸，触摸未使用）
**状态**：✅ **屏幕显示已实机验证正常**（文字方向、排布均正确）
**关联文档**：`SCREEN_BRINGUP_FINDINGS.md`（根因分析）、`docs/WATCH_BRINGUP_REPORT.md`（BLE 链路报告）

> ⚠️ 本节记录的全部显示参数均已在实机复核；未在实机验证的事项在 §6 显式列出。

---

## 1. 硬件与接线（已实机核对）

接线与固件常量**完全一致**，一次接对：

| 屏幕丝印 | 开发板 GPIO | 固件常量（Kconfig） |
|---|---|---|
| VCC | 3V3 | — |
| GND | GND | — |
| CS | GPIO10 | `KDisplayCsGpio` |
| RESET/RST | GPIO14 | `KDisplayRstGpio` |
| DC/RS | GPIO9 | `KDisplayDcGpio` |
| SDI/MOSI | GPIO11 | `KDisplaySpiMosiGpio` |
| SCK/CLK | GPIO12 | `KDisplaySpiSckGpio` |
| LED/BLK | GPIO21 | `KDisplayBlGpio` |
| SDO/MISO | 未接（单工） | `miso_io_num = -1` |
| 触摸 5 线（XPT2046） | 全部悬空 | 未启用 |

I2C 传感器总线（尚未接通，见 §6）：`SDA=GPIO18`、`SCL=GPIO8`（MAX30102 + MPU6500 共享）。

---

## 2. 调试过程：三个根因（每个都是"真根因"，非猜测）

### 根因 #1：白屏 —— 缺 `0x29 DISPON`

**现象**：接线全部正确，背光亮，但屏幕恒白；`raw_wire` 探针 6 条色带全部 `ESP_OK`、LVGL 定时器正常、无 panic。

**根因**：`esp_lcd_ili9341` 组件的 `panel_ili9341_init()` 只发 `SLPOUT` 与厂商序列表，**不发 `0x29 DISPON`**；`0x29` 只在 `panel_ili9341_disp_on_off()` 中发送，必须由应用显式调用。上电复位后控制器处于 sleep + display-off：照常 ACK 所有命令与 RAMWR（所以日志全绿），但输出级关断 → 恒白。

> 教训：**串口日志"代码跑完" ≠ "像素出现"**。此前 `SCREEN_BRINGUP_FINDINGS.md` 的"屏幕已点亮"结论属未验证的成功声明（未拍照比对）。

**修复**：探针在 `ili9341_init()` 后补 `send_cmd(0x29)`；生产路径 `display_driver.c` 本就有 `esp_lcd_panel_disp_on_off(panel, true)`（此路径正确）。

### 根因 #2：白屏连带缺陷 —— `trans_queue_depth = 0` + 全帧缓冲

**现象**：即使补上 DISPON，原生产配置仍无法显示。

**根因**（`SCREEN_BRINGUP_FINDINGS.md` §二/§三 实测）：
- `trans_queue_depth = 0`：esp_lcd SPI 在 0 时不创建传输队列，后续 `xQueueReceive` 断言崩溃；
- 全帧缓冲 240×320×2 = 150KB：PSRAM 关闭时落内部 SRAM，吃光内存。

**修复**（commit `bcda163`）：
- `trans_queue_depth = 10`；
- 绘制缓冲改为 **40 行条带 9,600px = 19,200B**（内部 DMA RAM），LVGL 自动分带重绘；
- `buff_dma=true` / `buff_spiram=false`：**显示路径不再依赖 PSRAM**；
- `CONFIG_DISPLAY_ENABLE=y`（此前为 `n`，显示代码根本没编译）。

### 根因 #3：镜像/颠倒 —— `esp_lvgl_port` 会覆写底层 MADCTL

**现象**：先后两次调 `esp_lcd_panel_mirror()`（先 `true,false` 后 `true,true`）屏幕**毫无变化**；用户最终确认实际问题仅为**左右镜像**（上下本来就正常）。

**根因**：`lvgl_port_add_disp()` 内部会调用 `lvgl_port_disp_rotation_update()`，用 `disp_cfg.rotation` **强制重写** `swap_xy`/`mirror`（`esp_lvgl_port_disp.c`）：

```c
case LV_DISPLAY_ROTATION_0:
    esp_lcd_panel_swap_xy(control_handle, disp_ctx->rotation.swap_xy);
    esp_lcd_panel_mirror(control_handle, disp_ctx->rotation.mirror_x, disp_ctx->rotation.mirror_y);
```

而 `disp_cfg.rotation` 一直是 `{false, false, false}` → 在底层调的任何 `esp_lcd_panel_mirror()` 都被立即冲掉。**必须在 `disp_cfg.rotation` 里配置**（底层初始化调用与其保持一致即可）。

**修复**（commit `2685fd0`）：`disp_cfg.rotation.mirror_x = true`、`mirror_y = false`；底层 `esp_lcd_panel_mirror(handle, true, false)` 同步（`MX=1, MY=0`，BGR 下 `MADCTL=0x48`）。

> 注：commit `eddc499`/`7223f38` 两次尝试因上述覆写机制而**无效**，已被 `2685fd0` 的正确方案取代——历史保留以便追溯"为什么改了没反应"。


---

## 3. 修复提交清单

| Commit | 内容 | 实机验证 |
|---|---|---|
| `bcda163` | 条带缓冲 + `trans_queue_depth=10` + 启用 `DISPLAY_ENABLE`（屏点亮） | ✅ 状态屏显示 |
| `eddc499` | 试图修镜像：`esp_lcd_panel_mirror(true, false)` | ❌ 无效（被 lvgl_port 覆写） |
| `7223f38` | 试图修镜像：补 `MY=1` | ❌ 无效（同上） |
| `2685fd0` | **`disp_cfg.rotation.mirror_x=true`（正确修法）** | ✅ 左右不再镜像 |
| — | 白屏根因 0x29：探针修复（`_screen_probe`，独立工程，未入库） | ✅ 探针色带可见 |

---

## 4. 实机验证证据（2026-09-26）

```
I (733)  main: TianshangPulse boot, model:v1.0.0, ESP32-S3, PSRAM disabled (no SPIRAM)
W (986)  main: sensor_init -> ESP_OK
I (995)  main: inference_engine_init -> ESP_ERR_NO_MEM        ← 已知（PSRAM 关，见 §6）
I (1189) main: offline_cache_init -> ESP_OK
I (1781) display: ILI9341 240x320 ready (SPI2 @10000000Hz, 40-row band buffer, 19200 B)
I (1979) ui: LVGL UI ready (ILI9341 240x320)
I (1979) main: ui_init -> ESP_OK
I (1979) main: main stack HWM=5896
```

- 状态屏三行文字（`TianshangPulse` / `HR   -- bpm` / `AF monitor: running`）显示与排布**目视确认正常**；
- 无 panic / assert / 重启循环；`main stack HWM=5896`（8192 栈，余量健康）；
- 复位方式：`python -m esptool --port COM3 run`（`%TEMP%\tp_reset.ps1` 可复用）。

---

## 5. 关键调参点（下次改显示只动这些）

| 参数 | 位置 | 当前值 | 说明 |
|---|---|---|---|
| SPI 时钟 | 生成版 `sdkconfig` / Kconfig `DISPLAY_SPI_CLOCK_HZ` | **10 MHz** | 首次点亮从 10MHz 起（20cm 杜邦线 40MHz 未经证实）；稳定后可升 40MHz |
| 镜像/旋转 | `display_driver.c` → `disp_cfg.rotation` | `mirror_x=true, mirror_y=false, swap_xy=false` | **唯一生效入口**（底层调用仅需保持一致） |
| 颜色序 | `display_driver.c` → `panel_cfg.rgb_ele_order` | BGR | 与 MADCTL BGR 位对应 |
| 条带高度 | `display_config.h` → `KDisplayBandRows` | 40 行（19,200B） | 减小可进一步降内存占用 |
| 字体 | `sdkconfig.defaults` | Montserrat 14/24 | 新字体必须在 Kconfig 显式启用 |
| 屏幕开关 | Kconfig `DISPLAY_ENABLE` | y（S3）/ n（P4 默认） | `DISPLAY_SIM` 为无屏骨架路径 |

---

## 6. 未在实机验证 / 已知遗留（bring-up 后续）

| # | 项 | 现状 | 下一步 |
|---|---|---|---|
| 1 | **PSRAM 未启用**（`PSRAM disabled (no SPIRAM)`） | `inference_engine_init -> ESP_ERR_NO_MEM`（TFLite arena 分配失败），LR 硬编码推理仍可用；Bug #1 未闭环 | 三件套诊断配置已就位（`SPIRAM_MEMTEST=n` + DEBUG 日志 + `BOOTLOADER_WDT=30000`），做一次"开启 PSRAM + 抓 `Pro cpu up`/`Starting app cpu` 检查点"实验钉死根因 |
| 2 | **传感器未接通** | 串口持续 `i2c ... nack` + `imu read failed`（MAX30102/MPU6500 未上电/未接） | 接通 I2C（SDA=18/SCL=8）后复测；心跳/血氧/AF 数据流 |
| 3 | TWDT 告警 | `task_wdt: - inference`（`KInferenceWaitTimeoutMs=10000` > TWDT 5s，无传感器窗时推理任务睡 10s 才喂狗） | 改分片等待（每 2s 喂一次）；属 `config.h`/`main.c` |
| 4 | SPI 时钟 10MHz | 首亮保守值 | 稳定后升 40MHz 实测 |
| 5 | 背光 PWM 调光 | 简单开关（GPIO21 拉高） | 配合 `POWER_BUDGET.md` 迭代 |
| 6 | 实时数据接线 | UI 为占位状态屏（`ui_manager.c` TODO） | HR/AF 推理结果 → LVGL 标签 |
| 7 | 功耗实测 | `POWER_BUDGET.md` §4 待回填 | 实测后移除"未在实机验证"标注 |

---

## 7. 复现流程（下次点亮或换屏）

```powershell
# 构建（S3）
powershell -File scripts\idf_build_s3_launch.ps1     # 等待 BUILD_EXIT_CODE=0

# 烧录（COM3）
powershell -File $env:TEMP\tp_flash_s3.ps1           # 等待 FLASH_EXIT_CODE=0

# 抓启动日志（8-10s）
powershell -File $env:TEMP\tp_monitor2.ps1 -seconds 10

# 软复位
powershell -File $env:TEMP\tp_reset.ps1              # esptool run
```

若屏幕全白：先查 `0x29`（`disp_on_off` 是否被调用），再查 `BL`（GPIO21 背光），最后查 `MOSI/SCK/CS` 是否错行——**顺序不要反，日志全绿不代表像素出现**。
若方向不对：只改 `disp_cfg.rotation`（§5），不要指望底层 `esp_lcd_panel_mirror()` 单独生效。

---

*最后更新：2026-09-26 · 显示链路实机验证通过*
