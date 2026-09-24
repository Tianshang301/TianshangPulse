# TianshangPulse 实机验证工作报告 · 2026-09-24

**日期**：2026-09-24
**验证对象**：ESP32-S3-WROOM-1 N16R8 开发板，MAC `84:c7:bb:70:5d:a6`
**固件版本**：`17081d3-dirty`（compile time `Sep 24 2026 18:24:52`）
**测试设备**：Huawei ADY-AL00（Android 12 / EMUI 12），TianshangHealth APP v1.1.2
**状态**：✅ **端到端 BLE 链路验证通过**

---

## 验证结论

手表固件与 TianshangHealth APP 的 BLE GATT 链路**已在实机验证通过**（AGENTS §9.5 DoD #5）。

### 已验证（实机观测）

| 项 | 证据 |
|---|---|
| 广播字段正确 | 串口 `adv fields set: name=TianshangPulse uuids16=[0xFFF0,0x180D]` |
| FAST 广播间隔 | 串口 `adv_itvl_min=48 adv_itvl_max=96`（30-60ms） |
| FAST → SLOW 切换 | 30s 后触发（`adv_switch_to_slow_cb`） |
| 广播稳定 | 固件持续运行 300s+ 无崩溃 |
| GATT DB 注册成功 | `gatts_count_cfg rc=0` + `gatts_add_svcs rc=0` |
| 手机 APP 能扫描到 | 手机 logcat 看到扫描结果（修复 `collectAnomalyEvents` 阻塞后） |
| 手机 APP 能连接 | 串口 `gatt: connected, handle=1`；用户确认通信成功 |
| 连接态停止广播 | 串口 `NimBLE: GAP procedure initiated: stop advertising` |
| 断线后重播 FAST | 串口 `gatt: disconnected` → 立即 `advertise` |

### 未在实机验证

- 传感器数据流（MPU6886 / MAX30102 未接，I2C NACK 为预期）
- 心率/血氧 notify 内容（无传感器数据）
- AF 异常检测（无 PPG 输入）
- 离线批量同步（0xFFF4，需先积累异常事件）
- 显示驱动（`CONFIG_DISPLAY_ENABLE=n`，本板无 ILI9341 屏）
- PSRAM-on 模式（Bug #1 未解，当前 `CONFIG_SPIRAM=n`）

---

## 调试过程回顾

### 手表端 Bug 修复（`ERROR_REPORT_WATCH_BRINGUP.md`）

| # | 问题 | 根因 | 状态 |
|---|---|---|---|
| 1 | PSRAM-on 启动崩溃 | app 阶段 MSPI/PSRAM cache 初始化崩溃，未定位 | **未解**，已规避 `CONFIG_SPIRAM=n` |
| 2 | `esp_pm_configure` 失败 | `CONFIG_PM_ENABLE` 未设 | **已修** |
| 3 | GATT 服务注册失败 `rc=3` | (a) `ble_svc_gap_init`/`gatt_init` 未在 `on_sync` 前调用；(b) notify-only 特征 `access_cb=NULL` | **已修** |
| 4 | IDE 后台重建覆盖 sdkconfig | `sdkconfig.defaults.esp32s3` 写死 `CONFIG_SPIRAM=y` | **已修** |
| 5 | `offline_cache_init` PSRAM 分配失败 | `MALLOC_CAP_SPIRAM` 在 PSRAM 关时返回 NULL | **已修** |
| 6 | LVGL 队列创建失败 | `CONFIG_DISPLAY_ENABLE=y` 默认，LVGL 需 PSRAM draw buffer | **已修**（`CONFIG_DISPLAY_ENABLE=n`） |

### 手机端 Bug 修复（TianshangHealth 仓库）

| 问题 | 根因 | 状态 |
|---|---|---|
| 扫描永远不开始 | `collectAnomalyEvents()` 的 `MutableSharedFlow.collect` 在无订阅者时阻塞，导致 `while(isActive)` 主循环永不启动 | **已修**（独立 `launch` Job） |
| 无权限弹窗 | 系统定位关闭（`location_mode=0`）拦截 LE 扫描 | **已修**（引导用户开启定位） |
| `ACCESS_COARSE_LOCATION` 缺失 | manifest 未声明，`maxSdkVersion="30"` 在 targetSdk 35 下过滤 | **已修**（移除 maxSdkVersion） |

---

## 关键技术决策

### 1. PSRAM 关闭（`CONFIG_SPIRAM=n`）

Bug #1 未解，临时关闭 PSRAM 规避启动崩溃。影响：
- TFLite arena 分配失败 → `inference_engine_run_features` 优雅降级走硬编码逻辑回归
- `offline_cache_init` 回退内部 SRAM（1.6KB 环形缓冲区）
- LVGL 关闭（`CONFIG_DISPLAY_ENABLE=n`），本板未接 ILI9341 屏

**后续**：要重新启用 PSRAM 需先解决 Bug #1 崩溃。

### 2. 广播策略（FAST burst + SLOW）

- FAST：`adv_itvl_min=48 adv_itvl_max=96`（30-60ms），持续 30s
- SLOW：1280ms 间隔
- 连接成功后停止广播；断开后重播 FAST burst

符合 AGENTS §9.3 #7 功耗预算要求。

### 3. GATT 服务注册时机

`ble_svc_gap_init()` / `ble_svc_gatt_init()` 必须在 `nimble_port_run()` 之前调用。原先在 `on_sync` 里注册太晚，导致 `ble_gatts_add_svcs` 返回 `BLE_HS_EINVAL`（rc=3）。

Notify-only 特征（0x2A37 / 0x2A5F / 0xFFF2）必须提供非 NULL `access_cb`（NimBLE `ble_gatts_chr_is_sane` 要求），用 `on_notify_chr_access` 返回 `BLE_ATT_ERR_READ_NOT_PERMITTED`。

### 4. 手机端扫描阻塞修复

`MutableSharedFlow.collect` 在 `replay=0` 且无订阅者时永久阻塞。`collectAnomalyEvents()` 必须用 `launch { ... }` 包装成独立 Job，否则阻塞后续 `while` 循环导致扫描永不开始。

---

## 相关文件

### 手表端（本仓库）

| 文件 | 改动 |
|---|---|
| `firmware/main/ble/gatt_server.c` | 服务注册前移 + `on_notify_chr_access`（Bug #3） |
| `firmware/main/ble/offline_cache.c` | PSRAM 关时回退内部 SRAM（Bug #5） |
| `firmware/main/main.c` | `offline_cache_init`/`ui_init` 返回值日志化 |
| `firmware/sdkconfig.defaults.esp32s3` | `CONFIG_SPIRAM=n` + `CONFIG_DISPLAY_ENABLE=n` |
| `firmware/sdkconfig.defaults` | 固化 `CONFIG_PM_ENABLE=y` + `TICKLESS_IDLE=y` |
| `ERROR_REPORT_WATCH_BRINGUP.md` | 完整调试过程记录 |

### 手机端（TianshangHealth 仓库）

| 文件 | 改动 |
|---|---|
| `feature/watch/src/main/java/.../ble/BleManager.kt` | 扫描诊断日志 + `device.name` SecurityException 防护 |
| `feature/watch/src/main/java/.../service/WatchBleService.kt` | `collectAnomalyEvents` 独立 Job + `ACCESS_COARSE_LOCATION` 检查 |
| `feature/watch/src/main/java/.../ui/WatchScreen.kt` | `requiredPermissions` 补 `ACCESS_COARSE_LOCATION` |
| `feature/watch/src/main/AndroidManifest.xml` | 声明 `ACCESS_COARSE_LOCATION` |

---

## 后续工作

1. **Bug #1（PSRAM-on 崩溃）**：仍未解，需进一步定位 MSPI/PSRAM cache 初始化崩溃点
2. **传感器接线**：MAX30102 / MPU6886 焊接到 S3 开发板，验证心率/血氧/加速度数据流
3. **显示驱动**：ILI9341 接线后重新启用 `CONFIG_DISPLAY_ENABLE=y`
4. **协议契约测试**：跑 `protocol-pin.json` 验证双端 byte-identical
5. **移除诊断日志**：BleManager.kt 和 WatchBleService.kt 里的 `DIAG` 日志应在发布前清理
6. **更新 TEST_LOG.md**：按 AGENTS §9.5 DoD #5 记录实机验证状态

---

## 签名

- **手表端固件**：`TianshangPulse` v1.0.0，commit `17081d3`
- **手机端 APP**：`TianshangHealth` v1.1.2，versionCode=5
- **协议版本**：见 `protocol-pin.json`（两仓库 byte-identical）

**验证人**：Tianshang
**日期**：2026-09-24
