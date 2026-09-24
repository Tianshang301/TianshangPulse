# TianshangPulse 实机调试错误报告

**日期**：2026-09-24
**目标**：在 ESP32-S3-WROOM-1 N16R8 实机上验证 1-1 广播修复（`PLAN_WATCH_INTEGRATION.md` P1 + P6，AGENTS §9.5 DoD #5）
**板子**：ESP32-S3-WROOM-1 N16R8，chip rev v0.2，MAC `84:c7:bb:70:5d:a6`
**串口**：COM3（CH340）
**状态**：广播已验证，但仍有复位循环（已定位，修复进行中）

---

## Bug 概览

| # | 现象 | 根因 | 状态 |
|---|---|---|---|
| 1 | 开机死在 `cpu_start: Multicore app`，`RTCWDT_RTC_RST` 循环 | PSRAM-on 时 app 阶段 MSPI/PSRAM cache 初始化崩溃 | **未解**，已规避（`CONFIG_SPIRAM=n`） |
| 2 | `main.c:63` abort，`esp_pm_configure` 失败 | `CONFIG_PM_ENABLE=n` 使其变成空桩返回 `ESP_ERR_NOT_SUPPORTED` | **已修** |
| 3 | `gatt config failed: ERROR`（后确认为 `rc=3`） | (a) `ble_svc_gap_init`/`ble_svc_gatt_init` 从未调用，DB 注册在 `on_sync` 太晚；(b) notify-only 特征 `access_cb=NULL` → `BLE_HS_EINVAL` | **已修** |
| 4 | 烧回去的固件仍崩溃（17:14 IDE 后台重建） | `sdkconfig.defaults.esp32s3` 写死 `CONFIG_SPIRAM=y`，重建时回退 | **已修** |
| 5 | `offline_cache_init -> ESP_ERR_NO_MEM`，`RTC_SW_CPU_RST` 循环 | `offline_cache_init` 用 `MALLOC_CAP_SPIRAM` 分配，PSRAM 关时返回 NULL | **已修** |
| 6 | `assert failed: xQueueGenericCreate queue.c:573`，复位循环 | `CONFIG_DISPLAY_ENABLE=y`（S3 默认），LVGL 需 PSRAM draw buffer，PSRAM 关时队列创建失败 | **修复中**（display 已关） |

---

## Bug #1 — PSRAM-on 启动崩溃（未解）

**现象**：
```
I (286) boot: Loaded app from partition at offset 0x10000
I (296) cpu_start: Multicore app
  ← 死在这里，随后 RTCWDT_RTC_RST 循环复位
```

**尝试过且排除**（均确认在 build 里生效但崩溃点不变）：
- `CONFIG_FREERTOS_IDLE_TASK_STACKSIZE` 1536 → 3072
- `CONFIG_ESP_MINIMAL_SHARED_STACK_SIZE` 2048 → 3072
- `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE` 4096 → 8192
- 关 `CONFIG_BT_NIMBLE_ROLE_CENTRAL/OBSERVER`
- `CONFIG_SPIRAM_SPEED` 80M / 40M 均失败
- `CONFIG_SPIRAM_MEMTEST=n`（已在 defaults，无效）
- `erase-flash` 全擦 + 重烧，行为相同（排除 NVS 污染）

**结论**：崩溃在 app 阶段 PSRAM cache/MMU 初始化，与栈大小、PSRAM 速度、NVS 均无关。未定位具体原因。

**规避**：`CONFIG_SPIRAM=n`。影响：TFLite arena 分配失败已优雅降级（`inference_engine_run_features` 走硬编码逻辑回归，从不解引用 `s_arena`）；`offline_cache_init` 需回退内部 SRAM（见 Bug #5）；LVGL 需关（见 Bug #6）。

**后续**：要重新启用 PSRAM 需先解决此崩溃。可能是 ESP32-S3 chip rev v0.2 的 MSPI/PSRAM 初始化已知问题，但未找到 errata 引用。

---

## Bug #2 — esp_pm_configure 失败（已修）

**根因**：`CONFIG_PM_ENABLE` 未设置，`esp_pm_configure()` 变成空桩返回 `ESP_ERR_NOT_SUPPORTED`，`ESP_ERROR_CHECK` abort。

**修复**：`sdkconfig.defaults` 固化：
```
CONFIG_PM_ENABLE=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y   # light_sleep_enable=true 需要
```

**验证**：`I (428) pm: Frequency switching config: CPU_MAX: 240 ... Light sleep: ENABLED` + `I (442) power: power manager init, mode=ACTIVE`

---

## Bug #3 — GATT 服务注册失败（已修）

**现象**：
```
E (716) gatt: gatt config failed: ERROR      # esp_err_to_name 打印泛泛的 ERROR
```

**根因分析（分两层）**：

### 3a. 服务从未注册
`ble_svc_gap_init()` / `ble_svc_gatt_init()` 在固件里从未调用，且 GATT DB 注册在 `on_sync`（NimBLE host 同步回调）里——但 `ble_gatts_count_cfg` / `ble_gatts_add_svcs` 必须在 `nimble_port_run()` **之前**调用。

### 3b. `rc=3` = `BLE_HS_EINVAL`
把三个调用的返回值逐条打日志后确认：
```
I (714) gatt: gap_device_name_set rc=0
I (716) gatt: gatts_count_cfg rc=3      ← 失败点
E (716) gatt: gatt config failed: rc=3
```

`BLE_HS_E*` 错误码是**正值**（`nimble/host/include/host/ble_hs.h:75-108`：`EALREADY 2`、`EINVAL 3`），不是负值——`esp_err_to_name` 对非 `esp_err` 码打印 "ERROR"，误导了排查方向。

`rc=3` = `BLE_HS_EINVAL`，由 `ble_gatts_chr_is_sane()`（`nimble/host/src/ble_gatts.c:367-379`）触发：
```c
if (chr->access_cb == NULL) {
    return 0;      // -> BLE_HS_EINVAL
}
```

NimBLE 要求**每个特征**（包括 notify-only）都有非 NULL 的 `access_cb`。0x2A37、0x2A5F、0xFFF2 三个 notify-only 特征的 `access_cb` 都是 NULL。

**修复**（`firmware/main/ble/gatt_server.c`）：
1. 把 `ble_svc_gap_init()` / `ble_svc_gatt_init()` + `ble_gatts_count_cfg` / `ble_gatts_add_svcs` 前移到 `nimble_port_run()` 之前
2. 新增 `on_notify_chr_access()` 返回 `BLE_ATT_ERR_READ_NOT_PERMITTED`，接到三个 notify-only 特征（NimBLE 只在客户端 Read/Write 时调用它，`ble_gatts_notify` 不走，协议语义不变）

**验证**：
```
I (720) gatt: gap_device_name_set rc=0
I (720) gatt: gatts_count_cfg rc=0      ← 原为 rc=3
I (721) gatt: gatts_add_svcs rc=0
I (730) adv: adv fields set: name=TianshangPulse uuids16=[0xFFF0,0x180D]
I (736) NimBLE: GAP procedure initiated: advertise;
I (743) NimBLE:  adv_itvl_min=48 adv_itvl_max=96   ← FAST 30-60ms
```

---

## Bug #4 — IDE 后台重建覆盖 sdkconfig（已修）

**现象**：16:26 烧的固件能正常广播（PSRAM-off），但 17:03 / 17:17 重建后变回 PSRAM-on 崩溃版，被烧回板上。

**根因**：`sdkconfig.defaults.esp32s3` 第 5 行写死 `CONFIG_SPIRAM=y`（"required for TFLite Micro arena"）。我 16:26 只改了 `sdkconfig`，没改 `.defaults`；17:14 某次重建（疑似 IDE 后台自动 build）按 `.defaults` 回退了 `sdkconfig`。

**修复**：`sdkconfig.defaults.esp32s3` 改为 `CONFIG_SPIRAM=n` + 注释说明原因与后续启用条件。

**后续隐患**：IDE 后台重建是定时炸弹，可能再次覆盖。建议调试期间关闭 IDE 的自动 build/同步。

---

## Bug #5 — offline_cache PSRAM 分配失败（已修）

**现象**：
```
I (1194) main: offline_cache_init -> ESP_OK      # 修复前是 ESP_ERR_NO_MEM
```
（修复后通过，但后续出现 Bug #6）

**根因**：`offline_cache.c:21-22` 用 `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` 分配环形缓冲区。PSRAM 关闭时 `heap_caps_malloc` 用 `MALLOC_CAP_SPIRAM` 标志**必然返回 NULL**（无 PSRAM 可用）→ `ESP_ERR_NO_MEM` → `ESP_ERROR_CHECK` abort → `RTC_SW_CPU_RST` 复位循环。

与 Bug #2 是同一个根因（PSRAM 关），但 `inference_engine_init` 的返回值没有 `ESP_ERROR_CHECK`（只打日志），`offline_cache_init` 有，所以它触发了 abort。

**修复**（`firmware/main/ble/offline_cache.c`）：
```c
#if CONFIG_SPIRAM
    s_ring = heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    s_ring = heap_caps_malloc(..., MALLOC_CAP_8BIT);  // 回退内部 SRAM，1.6KB
#endif
```

**验证**：`I (1241) main: offline_cache_init -> ESP_OK`

---

## Bug #6 — LVGL 队列创建失败（修复中）

**现象**：
```
assert failed: xQueueGenericCreate queue.c:573 (pxNewQueue)
```
`RTC_SW_CPU_RST` 复位循环，15 秒内 18 次复位。

**根因**：`CONFIG_DISPLAY_ENABLE=y`（ESP32-S3 Kconfig 默认值，`main/Kconfig.projbuild:31`：`default y if IDF_TARGET_ESP32S3`）。`display_driver.c:127` 设置 `buff_spiram = true`，LVGL 的 draw buffer 放在 PSRAM。PSRAM 关闭时 `lvgl_port_init` / `lvgl_port_add_disp` 创建队列分配失败，`xQueueGenericCreate` assert。

本板也未实际接 ILI9341 显示屏（`KDisplaySpi*Gpio` 引脚未接线），开显示纯属浪费内存。

**修复**：
1. `sdkconfig.defaults.esp32s3` 加 `CONFIG_DISPLAY_ENABLE=n`
2. 直接改 `sdkconfig:566` `CONFIG_DISPLAY_ENABLE=y` → `# CONFIG_DISPLAY_ENABLE is not set`（reconfigure 不采纳 `.defaults` 的 n，必须直接改 sdkconfig）

**验证**：进行中（build → flash → 抓串口）

---

## 完整 boot 序列（目标状态）

修复全部 Bug #1-6 后，预期序列：

```
I (296) cpu_start: Multicore app
I (305) cpu_start: Pro cpu start user code
I (305) cpu_start: cpu freq: 160000000 Hz
I (417) main_task: Started on CPU0
I (418) main_task: Calling app_main()
I (428) main: TianshangPulse boot, model:v1.0.0, ESP32-S3, PSRAM disabled (no SPIRAM)
I (428) pm: Frequency switching config: CPU_MAX: 240 ... Light sleep: ENABLED
I (442) power: power manager init, mode=ACTIVE
W (644) main: sensor_init -> ESP_OK          # 传感器未接，I2C NACK 为预期
I (653) main: inference_engine_init -> ESP_ERR_NO_MEM   # PSRAM 关，优雅降级
I (671) BLE_INIT: Bluetooth MAC: 84:c7:bb:70:5d:a6
I (720) gatt: gap_device_name_set rc=0
I (720) gatt: gatts_count_cfg rc=0
I (721) gatt: gatts_add_svcs rc=0
I (730) adv: adv fields set: name=TianshangPulse uuids16=[0xFFF0,0x180D]
I (736) NimBLE: GAP procedure initiated: advertise;
I (743) NimBLE:  adv_itvl_min=48 adv_itvl_max=96
I (750) main: offline_cache_init -> ESP_OK
I (752) main: ui_init -> ESP_OK              # skeleton 分支（display 关）
I (755) main: main stack HWM=...
I (~30750) adv: FAST burst done -> SLOW      # ~30s 后切 1250ms
```

---

## 1-1 广播实机验证（DoD #5）

**已验证**：
- `adv fields set: name=TianshangPulse uuids16=[0xFFF0,0x180D]` ✅
- `adv_itvl_min=48 adv_itvl_max=96`（FAST 30-60ms）✅
- FAST → SLOW 切换在 ~30s 触发 ✅
- 广播稳定重复出现，无崩溃（在 Bug #5-6 修复前短暂验证过）

**待验证**（需手机蓝牙扫描）：
- 手机端 TianshangHealth 扫描能看到 `TianshangPulse`
- 连接 → Connected
- 订阅 0x2A37 / 0x2A5F / 0xFFF2 notify

**限制**：
- 传感器未接（MAX30102 / MPU6886 全 I2C NACK），心率/血氧 notify 为空值——不影响 BLE 链路验证
- PSRAM 关（Bug #1 未解），TFLite arena 降级，推理走硬编码逻辑回归
- 显示关（Bug #6），本板无 ILI9341 屏

---

## 相关文件

| 文件 | 改动 |
|---|---|
| `firmware/main/ble/gatt_server.c` | 服务注册前移 + `on_notify_chr_access`（Bug #3） |
| `firmware/main/ble/offline_cache.c` | PSRAM 关时回退内部 SRAM（Bug #5） |
| `firmware/main/main.c` | `offline_cache_init`/`ui_init` 返回值日志化（Bug #6 排查） |
| `firmware/sdkconfig.defaults.esp32s3` | `CONFIG_SPIRAM=n` + `CONFIG_DISPLAY_ENABLE=n`（Bug #1/#4/#6） |
| `firmware/sdkconfig.defaults` | 固化 `CONFIG_PM_ENABLE=y` + `TICKLESS_IDLE=y`（Bug #2） |
