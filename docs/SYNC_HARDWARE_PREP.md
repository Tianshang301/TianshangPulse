# 硬件到货前软件准备 · 同步文档

> **文档性质**：一次性**状态同步 / 交接**文档。记录"16MB Flash 切换 + ILI9341 显示驱动"这一轮
> 硬件到货前软件准备的完工状态、验证证据、配置终态与到货后 bring-up 清单。
> **协议同步点声明**：本轮**未触碰** GATT 协议、广播行为与 `docs/PROTOCOL.md`，
> 因此 `protocol-pin.json` / `docs/PROTOCOL_VECTORS.md` **无需重算**（AGENTS.md §9.2 六步流程未触发）。
> **状态图例**：✅ 已完工且已实跑验证 ｜ ⏳ 已实现但**未在实机验证**（板卡在途） ｜ 📋 下一步待办

---

## 1. 本轮范围与边界

| 项 | 内容 |
| --- | --- |
| **目标** | ①固件由 8MB 切至 16MB Flash；②加入 ILI9341 SPI 显示驱动与 LVGL 界面骨架 |
| **范围外（未触碰）** | GATT 协议与实现、广播行为、`offline_cache`、`power_manager`、PPG 信号链与预处理、模型与推理路径 |
| **前置阻塞** | 无（开发板在途，本轮全部工作为纯软件侧） |
| **不可逆改动** | 无（分区表与组件依赖均可回退；旧 sdkconfig 已备份，见 §11） |

---

## 2. 硬件到货清单（已下单，¥97.82）

| 器件 | 型号 | 备注 |
| --- | --- | --- |
| 主控 | ESP32-S3-N16R8 开发板 | 16MB Flash + 8MB OPI PSRAM；向下焊接 |
| 屏幕 | 2.8" TFT ILI9341 240×320（SPI，带触摸） | 触摸引脚**未启用**（悬空），固件不驱动触摸 |
| PPG | MAX30102 | 绿板，已预焊 |
| IMU | MPU6500 | MPU6886 的替代品；I2C 地址同为 `0x68`，驱动**无 WHO_AM_I 校验**故兼容 |
| 连线 | 60 根杜邦线 + 2 块 MB-102 面包板 | bring-up 阶段使用 |

> ⏳ **MPU6500 未经实机验证**：驱动兼容性基于"同地址 + 无 WHO_AM_I 检查"推断，
> 需上板确认加速度/陀螺仪读数正常（见 §7 步骤 5）。

---

## 3. 完工状态对照（原 4 步计划）

| # | 计划项 | 状态 | 证据摘要 |
| --- | --- | --- | --- |
| 1 | 16MB Flash 切换 | ✅ | `partitions_16mb.csv`、`sdkconfig.defaults.esp32s3`、`platform_esp32s3.h`；生成区 `flash_args` 实证 `--flash_size 16MB` |
| 2 | ILI9341 显示驱动 | ⏳ | 2 个托管组件 + `ui/display_config.h` + `ui/display_driver.{c,h}` + Kconfig 菜单 + `ui_manager.c` 重写；**编译通过，未上板** |
| 3 | 验证 | ✅ | S3 构建 0、P4 回归构建 0、`parity_check.py` PASS、host 单测 ALL PASS |
| 4 | 文档同步 | ✅ | `HARDWARE.md`、`MEMORY_LAYOUT.md`、`README.md`、`docs/README.zh-CN.md`、`POWER_BUDGET.md` |
| 5 | 提交与上传 | ✅ | `b418e63` + `7f5f576`，GitHub `main` 远端 tip 已核实一致 |
| 6 | 密钥安全审计 | ✅ | 工作区 / 跟踪文件 / **完整 Git 历史**均无密钥；`.gitignore` 已加固 |

---

## 4. 变更清单（提交记录）

### 4.1 提交

| 提交 | 类型 | 说明 | 规模 |
| --- | --- | --- | --- |
| `b418e63` | `feat(s3)` | 切换 16MB Flash + 新增 ILI9341 显示驱动与 LVGL UI | 17 文件，+405 / −24 |
| `7f5f576` | `chore(security)` | `.gitignore` 增加 `.env`/API Key/密钥类文件忽略规则 | 1 文件，+13 |

### 4.2 新增文件

| 文件 | 作用 |
| --- | --- |
| `firmware/partitions_16mb.csv` | 16MB 分区表（`nvs`/`phy_init`/`factory`/`model` 与 8MB 表**逐字节一致**，仅 `storage` 增大） |
| `firmware/main/ui/display_config.h` | 显示层全部常量与 Kconfig 封装；**bring-up 调参唯一入口** |
| `firmware/main/ui/display_driver.{c,h}` | SPI2 总线 + `esp_lcd_ili9341` 面板 + `esp_lvgl_port` 接线，含失败回滚 |
| `scripts/idf_build_s3_launch.ps1` / `idf_build_p4_launch.ps1` | 无人值守构建启动器（内含 IDF 环境 source），可留作便利脚本或删除 |

### 4.3 修改文件

| 文件 | 变更 |
| --- | --- |
| `firmware/sdkconfig.defaults.esp32s3` | `FLASHSIZE_16MB` + `partitions_16mb.csv`（PSRAM 保持 OCT 80M 不变） |
| `firmware/sdkconfig.defaults` | 新增 `LV_FONT_MONTSERRAT_14/24`（状态屏大号数字所需） |
| `firmware/main/platform/platform_esp32s3.h` | `KPlatformFlashSize` → 16MB |
| `firmware/main/Kconfig.projbuild` | 新增 "TianshangPulse Display Configuration" 菜单（7 项） |
| `firmware/main/idf_component.yml` | 新增 `espressif/esp_lvgl_port ^2.9.0`、`espressif/esp_lcd_ili9341 ^2.1.0` |
| `firmware/main/CMakeLists.txt` | 源文件 `ui/display_driver.c`；依赖 `esp_lcd` + 两个托管组件 |
| `firmware/main/ui/ui_manager.c` | 重写：状态屏 + `CONFIG_DISPLAY_ENABLE` 守卫（保证 P4 / 无屏板可编译可运行） |
| `docs/HARDWARE.md` | 屏幕选型、SPI2 引脚表、BOM 状态、"未在实机验证"声明 |
| `docs/MEMORY_LAYOUT.md` | S3 → N16R8、LVGL 绘制缓冲条目、新增 Flash 分区章节（两表对照） |
| `docs/POWER_BUDGET.md` | 屏幕功耗行（标"未实测"） |
| `README.md` / `docs/README.zh-CN.md` | 目录树、目标芯片 N16R8、组件清单（英中同步） |
| `.gitignore` | 密钥类文件忽略规则 |

---

## 5. 验证证据（均为实跑记录）

| 验证项 | 命令 / 方法 | 结果 |
| --- | --- | --- |
| S3 构建 | `idf.py set-target esp32s3` + `idf.py build` | ✅ `BUILD_EXIT_CODE=0`；`TianshangPulse.bin` = 0xEA140（936.3KB）；`app` 分区 0x300000，剩余 0x215EC0（**70%**）；bootloader 0x5210 |
| P4 回归构建 | `idf.py set-target esp32p4` + `idf.py build` | ✅ `BUILD_EXIT_CODE=0`（显示默认关闭，仅日志骨架） |
| 分区表实证 | 读取生成区 `build/flash_args` | ✅ `--flash_size 16MB`（确认走的是新分区表与新 Flash 容量） |
| 信号链一致性 | `python scripts/parity_check.py` | ✅ `verdict: PASS`（`n_peaks mismatch: 0`；训练-推理特征相对偏差 ~1e-7 量级） |
| host 单测 | `python -m ziglang cc … test_offline_cache.c` | ✅ `ALL PASS` |
| 组件解析 | 构建日志 `NOTICE: [n/7]` | ✅ `cmake_utilities 0.5.3`、`esp-nn 1.3.2`、`esp-tflite-micro 1.4.0`、`esp_lcd_ili9341 2.1.0`、`esp_lvgl_port 2.9.0`、`lvgl 9.6.0` |
| 构建现场状态 | 读取生成区 `sdkconfig` | ✅ `CONFIG_IDF_TARGET="esp32s3"`、`FLASHSIZE="16MB"`、`SPIRAM_MODE_OCT=y`、`PARTITION_TABLE_CUSTOM_FILENAME="partitions_16mb.csv"` |
| 提交可见性（AGENTS §9.2 自检） | `git check-ignore` + `git status --short` | ✅ 任务文件均可见、未被误忽略 |
| 密钥审计（3 层） | 跟踪文件扫描 / 工作区扫描 / `git log --all -p --full-history` 全历史扫描 | ✅ 均无命中；`.gitignore` 加固后用 6 个 dummy 文件回归（`check-ignore` exit 0 全部被忽略） |
| GitHub 同步 | `git ls-remote origin main` | ✅ 远端 tip `7f5f576…` = 本地 HEAD = `origin/main`，无 ahead/behind |

### 5.1 构建中适配的一个组件 API 差异（备查）

`esp_lvgl_port 2.9.0` 的 `lvgl_port_display_cfg_t` **没有 `mipi_dsi` 字段**（官方 README 示例仍列该字段，属文档滞后）。
MIPI-DSI 屏改由独立接口 `lvgl_port_add_disp_dsi()` 承担。已按本地头文件实际定义修正 `display_driver.c`。
> 教训：托管组件文档可能与实际头文件不同步，**以 `managed_components/…/include/*.h` 为准**。

---

## 6. 配置终态快照（后续勿误改）

### 6.1 分区表对照

| 分区 | 偏移 | 8MB（`partitions_8mb.csv`，legacy） | 16MB（`partitions_16mb.csv`，**当前 S3 目标**） |
| --- | --- | --- | --- |
| nvs | 0x9000 | 0x6000 | 0x6000（相同） |
| phy_init | 0xf000 | 0x1000 | 0x1000（相同） |
| factory（app） | 0x10000 | 0x300000 | 0x300000（相同） |
| model | 0x310000 | 0x100000 | 0x100000（相同） |
| storage | 0x410000 | 0x3F0000（~3.9MB） | **0xBF0000（~12MB）** |

> 两张表的**前四个分区逐字节一致**，切换 Flash 容量不影响 `nvs`/OTA 语义与模型区偏移。
> CSV **不支持行内注释**，注释必须独立成行（历史教训，见 AGENTS.md §8.1）。

### 6.2 组件依赖（`firmware/main/idf_component.yml`）

| 依赖 | 声明 | 实际解析 |
| --- | --- | --- |
| `idf` | `>=5.4` | ESP-IDF v5.4.0（`C:\Espressif\esp-idf`） |
| `espressif/esp-tflite-micro` | `^1.1.0` | 1.4.0 |
| `lvgl/lvgl` | `^9.2.0` | 9.6.0 |
| `espressif/esp_lvgl_port` | `^2.9.0` | 2.9.0 |
| `espressif/esp_lcd_ili9341` | `^2.1.0` | 2.1.0 |

> 关键前提：**IDF v5.4 内置 `esp_lcd` 无 ILI9341 驱动**（仅有 nt35510/ssd1306/st7789），
> 必须引入托管组件，切勿改回内置面板 API。

### 6.3 引脚分配（S3）与规避清单

| 用途 | GPIO |
| --- | --- |
| I2C（MAX30102 + MPU6500 共享） | SDA=18, SCL=8 |
| 屏幕 SPI2 | SCK=12, MOSI=11, CS=10, DC=9, RST=14, BL=21 |
| **不可占用** | 26–32（板载 Flash SPI0/1）、33–37（OPI PSRAM 数据线）、0/3/45/46（Strapping）、19/20（USB OTG）、43/44（UART0） |
| 屏幕触摸 5 线 | 悬空未接（固件未驱动触摸） |

### 6.4 显示层实现要点（`ui/display_driver.c`）

- 总线：`SPI2_HOST`，`pclk_hz = CONFIG_DISPLAY_SPI_CLOCK_HZ`（默认 40MHz），模式 0
- 面板：`esp_lcd_new_panel_ili9341`，16bpp；**当前 `LCD_RGB_ELEMENT_ORDER_BGR`**（常见模块默认）
- 传输：`trans_queue_depth = 0` → **同步传输**（~30ms/帧，状态屏足够，且 flush 顺序天然正确）
- 缓冲：150KB 全帧绘制缓冲置于 **PSRAM**（`buff_spiram = true`），小块 SRAM 流式搬运至 SPI DMA
- 内存约束：符合 MEMORY_LAYOUT §2（大缓冲入 PSRAM）与 §4（未在 ISR 中分配堆）

---

## 7. 实机 bring-up 清单（板卡到货后按序执行）

| # | 步骤 | 预期现象 / 判据 |
| --- | --- | --- |
| 0 | **核对开发板原理图**：GPIO8/18（I2C）与 GPIO12/11/10/9/14/21（屏幕）未被板载外设占用 | 无冲突方可接线 |
| 1 | **基线**：不接屏幕先烧录（`bash idf.py -p COMx flash monitor`） | 日志出现 "LVGL UI ready (ILI9341 …)"（此时屏幕无显示属正常）；确认传感器/BLE 日志正常 |
| 2 | 接屏幕 6 线（SCK/MOSI/CS/DC/RST/BL，**先不接触摸**） | — |
| 3 | 烧录并观察日志 | `ILI9341 240x320 ready (SPI2 @40000000Hz, PSRAM draw buffer)` + `LVGL UI ready` |
| 4 | 按 §7.1 症状表调参（**每次只改一项**，改完重新 build+flash） | 画面正常：深底 + 标题 + HR 占位 + AF 运行态 |
| 5 | 接 MAX30102 与 MPU6500，I2C 扫描 | 地址 `0x57`（MAX30102）与 `0x68`（MPU6500）可见；PPG/IMU 读数合理 |
| 6 | 记录实测功耗（各电源模式） | 回填 `docs/POWER_BUDGET.md` §4 |
| 7 | 实机验证完成后**移除文档中的"未在实机验证"标注** | `HARDWARE.md` §3、`POWER_BUDGET.md`、本文档 §3 状态由 ⏳ 转 ✅ |

### 7.1 症状 → 原因 → 处置（全部在 `ui/display_config.h` 或 `display_driver.c` 一处可改）

| 症状 | 可能原因 | 处置 |
| --- | --- | --- |
| 全白 / 全黑，背光不亮 | 背光未供电 / `BL` 接错 | 量 `BL` 引脚电平；必要时 `DISPLAY_BL_GPIO = -1` 并把屏的 LED 直连 3V3 |
| 全白 / 全黑，背光亮 | `CS`/`DC` 接错、`RST` 未接 | 逐线核对；`RST` 可设 `-1` 交由软件复位 |
| 花屏 / 噪点 / 偶发错位 | 40MHz 超面包板信号完整性 | `DISPLAY_SPI_CLOCK_HZ` 降至 26000000 或 20000000 |
| 红蓝互易（颜色反） | RGB 元素序 | `display_driver.c` 中 `rgb_ele_order` 由 `BGR` 改 `RGB` |
| 底片感（反色） | 面板 invert | `esp_lcd_panel_invert_color(…, true)` |
| 画面镜像 / 横竖颠倒 | mirror / swap | `esp_lcd_panel_mirror()` / `swap_xy()`，或 `disp_cfg.rotation` |
| 图像错行 / 偏移一个像素 | 字节序 | `disp_cfg.flags.swap_bytes` 取反 |
| 开机即重启 / 棕色掉电 | 面包板供电不足（屏 + BLE 峰值） | 屏与板分别供电、共地；缩短杜邦线 |

> ⏳ 表中**所有处置均为预案，未经实机验证**；实际根因以上板日志为准。

---

## 8. 已知未验证项与风险

| # | 项 | 性质 | 影响面 | 处置 |
| --- | --- | --- | --- | --- |
| 1 | 屏幕方向 / 反相 / 颜色序 / `swap_bytes` | ⏳ 未实机 | 仅显示观感 | §7.1 单点调参 |
| 2 | 40MHz SPI 在面包板上的稳定性 | ⏳ 未实机 | 显示花屏 | 降频至 26MHz |
| 3 | MPU6500 替代 MPU6886 | ⏳ 未实机 | IMU 读数 | §7 步骤 5 验证；必要时补 WHO_AM_I 探测与量程寄存器核对 |
| 4 | 屏幕 + 背光功耗 | ⏳ 未实机 | 30mA 目标可能被击穿 | 先记录实测再决定 PWM 调光 / 占空比策略 |
| 5 | LVGL 任务栈与 CPU 占用 | ⏳ 未实机 | 与 25Hz 推理共存 | `uxTaskGetStackHighWaterMark()` 抽查；必要时提高 LVGL 任务优先级配置 |
| 6 | 背光为**常开 GPIO**（非 PWM） | 已知取舍 | 功耗 | 后续迭代接 LEDC PWM |
| 7 | 触摸未启用 | 已知取舍 | 交互 | 需要交互时再接 5 线并引入 `esp_lcd_touch` |

---

## 9. 文档同步义务核对（AGENTS.md §5 / §9）

| 触发类型 | 是否触发 | 已更新文档 |
| --- | --- | --- |
| 新增传感器 | ❌ 未触发（无新器件类型，仅屏幕显示） | — |
| **修改 GATT 协议** | ❌ **未触发** | — |
| 新增/修改特征值 | ❌ 未触发 | — |
| 修改广播/连接行为 | ❌ 未触发 | — |
| 模型结构变更 | ❌ 未触发 | — |
| 内存布局调整 | ✅ 触发（新增 LVGL 绘制缓冲 + Flash 分区变更） | `docs/MEMORY_LAYOUT.md` |
| 新增构建依赖 | ✅ 触发（2 个托管组件） | `README.md`、`docs/README.zh-CN.md`、`docs/SYNC_HARDWARE_PREP.md` |
| 硬件新增器件（屏幕） | ✅ 触发 | `docs/HARDWARE.md`、`docs/POWER_BUDGET.md` |

> **协议四同步点（`protocol-pin.json` / `PROTOCOL.md` / `PROTOCOL_VECTORS.md` / 两仓 AGENTS 接线章节）本轮全部未变**，
> 故**未重算 SHA-256、未复制到 TianshangHealth** —— 这符合 AGENTS.md §9.2"生成方向不可逆"的约束（仅协议变更时才动）。

---

## 10. 遗留项与下一步

### 10.1 本轮内顺手修掉的小瑕疵

| 项 | 处置 |
| --- | --- |
| `docs/HARDWARE.md` 平台表仍写 "WROOM-1-N8R8 / 8MB"（与已切换的 N16R8 矛盾） | ✅ 已改为 N16R8 / 16MB |
| `docs/HARDWARE.md` IMU 行未标注 MPU6500 替代 | ✅ 已补注（同地址 0x68、驱动无 WHO_AM_I 校验） |
| `docs/POWER_BUDGET.md` 屏幕功耗行缺失（P4 表写"待选型"、S3 表无该行） | ✅ 已补 ILI9341 行并标"未实测" |
| `docs/POWER_BUDGET.md` P4 表 IMU 行仍写 MPU6886、低功耗 checklist 缺屏幕背光项 | ✅ 已改为 MPU6500 / MPU6886；已补背光 PWM 待办项 |

### 10.2 待办（不阻塞，按优先级）

| 优先级 | 项 | 归属 |
| --- | --- | --- |
| P1 | 实机 bring-up（§7），完成后清"未在实机验证"标注 | 到货后 |
| P1 | 与 TianshangHealth 接线主线（`PLAN_WATCH_INTEGRATION.md` 任务卡 P1–P9） | 既有主线，独立于本轮 |
| P2 | UI 实时数据接线：HR/SpO2/AF 结果刷新到状态屏（`ui_manager.c` 内 TODO） | 后续迭代 |
| P2 | 背光 LEDC PWM 调光（依赖实测功耗数据） | 后续迭代 |
| P3 | CI 缺失（仓库无 `.github/`，见 AGENTS §9.5 任务卡 P8） | 后续 |
| P3 | 触摸（`esp_lcd_touch` + ILI9341 触摸控制器） | 按需 |

---

## 11. 交接备注

### 11.1 构建环境

| 项 | 值 |
| --- | --- |
| ESP-IDF | v5.4.0 @ `C:\Espressif\esp-idf` |
| 工具链 | `xtensa-esp-elf 14.2.0_20241119`；ccache 已启用 |
| 当前目标 | `esp32s3`（构建现场即为可烧录状态） |
| 构建命令 | `cd firmware; idf.py build`；便利脚本 `scripts/idf_build_s3_launch.ps1` / `idf_build_p4_launch.ps1` |
| 旧 8MB 配置备份 | `%TEMP%\sdkconfig.bak-8mb`（如需回退 8MB：恢复该文件并改回 `partitions_8mb.csv`） |

### 11.2 边界声明（重要）

- 本轮的**全部变更已提交并推送**，工作区中**任务相关文件无任何未提交改动**（`git status` 对任务文件路径为空）。
- 工作区仍存在**与本轮无关的另一条主线 WIP（未提交）**：
  `gatt_server.c`、`offline_cache.{c,h}`、`max30102.c`、`signal_gate.c`、`config.h`、`main.c`、
  `docs/PROTOCOL_VECTORS.md`、`protocol-pin.json`、`PLAN*.md`、`tests/`、`scripts/parity_check.py`、
  `sensors/ppg_preprocess.{c,h}` 等。**本轮全程未触碰这些文件**。
- **未在实机验证**：本文档所有 ⏳ 标记项均**未在真实硬件上验证**；
  在完成 §7 步骤 7 之前，任何"已通过"表述仅指**编译/主机侧验证**。

---

*创建：2026-09-23 ｜ 适用版本：ESP32-S3 目标 @ `7f5f576` ｜ 维护者：Tianshang301*
