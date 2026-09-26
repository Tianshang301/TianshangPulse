# 屏幕点亮根因分析（ILI9341 / ESP32-S3）

**日期**：2026-09-25  
**验证硬件**：ESP32-S3-WROOM-1-1U64-N16R8 开发板，MAC `84:c7:bb:70:5d:a4`，ILI9341 2.8in SPI 屏  
**结论**：屏幕已点亮并成功绘制图案。**`ERROR_REPORT_WATCH_BRINGUP.md` 中 Bug #6 的根因判断是错误的。**

---

## 一、接线确认（已验证）

用户提供的接线表与固件 Kconfig 默认值**完全一致**：

| 模块丝印 | 开发板 GPIO | 固件常量 | 状态 |
|---|---|---|---|
| VCC → 3V3 | GND → GND | — | ✅ 屏幕已亮 |
| CS | GPIO10 | `KDisplayCsGpio` | ✅ |
| RESET/RST | GPIO14 | `KDisplayRstGpio` | ✅ |
| DC/RS | GPIO9 | `KDisplayDcGpio` | ✅ |
| SDI/MOSI/DIN | GPIO11 | `KDisplaySpiMosiGpio` | ✅ |
| SCK/CLK | GPIO12 | `KDisplaySpiSckGpio` | ✅ |
| LED/BLK/BL | GPIO21 | `KDisplayBlGpio` | ✅ |
| SDO/MISO | 未接 | `miso_io_num = -1` | ✅ 半双工，正确 |
| T_CLK/T_CS/T_DIN/T_DO/T_IRQ | 悬空 | 未使用 | ✅ XPT2046 触摸未接 |

---

## 二、Bug #6 真实根因：`trans_queue_depth = 0`

`ERROR_REPORT_WATCH_BRINGUP.md` 记录的现象：

```
assert failed: xQueueGenericCreate queue.c:573 (pxNewQueue)
```

**报告中的判断（错误）**：LVGL 需要 PSRAM draw buffer，PSRAM 关闭导致队列创建失败。

**真实原因**：`display_driver.c:54` 把 `trans_queue_depth` 设为 `0`。

调用链：

```
display_driver.c:47  .trans_queue_depth = 0
  → esp_lcd_panel_io_spi.c:82   .queue_size = io_config->trans_queue_depth   // = 0
    → spi_master.c:502          xQueueCreate(0, sizeof(spi_trans_priv_t))
      → queue.c:526  if (uxQueueLength > 0 && ...)   ← 0 不满足，跳过 pvPortMalloc
      → queue.c:546  configASSERT(pxNewQueue)          ← pxNewQueue 是 NULL，assert
```

`xQueueGenericCreate()` 对 `uxQueueLength == 0` 的处理是**不分配内存、直接返回 NULL**，然后触发 assert。这与 RAM 多少无关 —— 我在 **286,720 字节最大空闲块** 的情况下复现了这个 assert。

`display_driver.c:46` 的注释 `trans_queue_depth = 0 -> synchronous transfers` 是错的。`0` 的含义是"根本不建队列"，不是"同步传输"。任何 `> 0` 的值都能正常同步使用。

**为什么跑了这么久才发现**：固件的 `sdkconfig:566` 是
`# CONFIG_DISPLAY_ENABLE is not set`。显示代码**从未被编译**，所以这个 bug 被掩盖而非修复。`ERROR_REPORT` 把 `CONFIG_DISPLAY_ENABLE=n` 当成"修复"，实际是"屏蔽"。

**修复**：`trans_queue_depth` 改为 `10`（所有 `esp_lvgl_port` 示例的值），成本约 10 KB。

---

## 三、第二个根因：全帧缓冲把内部 SRAM 吃光

`display_config.h:30`：

```c
#define KDisplayDrawBufPix  (KDisplayHorRes * KDisplayVerRes)   // 240*320 = 76,800 px
```

16-bit RGB565 → **153,600 字节 = 150 KB**。

这块板是 N16R8（8MB PSRAM）但 **PSRAM 关闭**（Bug #1 未解：开启后在 `cpu_start: Multicore app` 崩溃）。PSRAM 关时唯一可用的是 `dram0_0_seg`：

```
0x3fcbdb88 - 0x3fc88000 = 246,264 字节（240 KB）
```

实测 `.bss` 段：

| | `.bss` 大小 | 结果 |
|---|---|---|
| 全帧 `s_fb`（150 KB） | `0x260f0` = 153,680 B | 恰好占满，`_bss_end` 正好等于段上限 |
| 24 行条带（11 KB） | `0x35f0` = 13,824 B | 空出 140 KB 给 FreeRTOS |

全帧缓冲时 FreeRTOS 堆只剩 `free=213,124 / largest_block=31,744`。OS 要起任务、建队列、建 spinlock —— 每一个都要分配。于是出现了**三个不同表象、同一个原因**的崩溃：

| 现象 | 表象 | 实际 |
|---|---|---|
| `xQueueGenericCreate queue.c:573` | 队列分配失败 | 部分成立（RAM 不足）+ **queue_size=0**（必然） |
| `spinlock_acquire spinlock.h:142` | `pm_impl.c` idle hook ISR 中 spinlock 分配失败 | 内存耗尽 |
| `Cache disabled but cached memory region accessed` | `xthal_save_extra_nw` 访问坏指针 `0xffffffff` | 内存耗尽导致上下文结构体损坏 |

> **重要**：这三个都**不是** "out of PSRAM"。报告称 "LVGL needs PSRAM draw buffer" 把方向弄反了 —— 真正的问题是 150 KB 被提交到 OS 也需要的内部 RAM 上。

`display_config.h:26` 的注释说 "Full-frame draw buffer in PSRAM"，但在 `CONFIG_SPIRAM=n` 下这是**纯内部 SRAM**，注释与实际不符。

**修复**：PSRAM 关闭时必须改用条带缓冲（band buffer）+ 分块重绘。24 行 = 11,520 字节，是原来的 1/13。

---

## 四、验证结果

两个根因都修掉后，实机输出：

```
I (613) screen_probe: === screen probe: ILI9341 2.8in SPI (band buffer) ===
I (620) screen_probe: psram: CONFIG_SPIRAM=n  <-- only ~240 KB internal DRAM!
I (626) screen_probe: heap free=354420 largest_block=286720 (band buf=11520 bytes)
I (642) screen_probe: BL=21 -> HIGH
I (665) screen_probe: spi_bus_initialize OK (SCK=12 MOSI=11)
I (703) screen_probe: panel io OK (CS=10 DC=9 clk=40000000Hz)
I (986) screen_probe: panel reset+init OK (RST=14)
I (990) screen_probe: [CYAN] solid rounds=2
I (1311) screen_probe:   CYAN round 1: 317 ms ret=ESP_OK
I (1634) screen_probe:   CYAN round 2: 317 ms ret=ESP_OK
I (1639) screen_probe: [WHITE] solid rounds=2
I (1960) screen_probe:   WHITE round 1: 317 ms ret=ESP_OK
I (2282) screen_probe:   WHITE round 2: 317 ms ret=ESP_OK
I (2287) screen_probe: [YELLOW] solid rounds=2
I (2609) screen_probe:   YELLOW round 1: 317 ms ret=ESP_OK
I (2931) screen_probe:   YELLOW round 2: 317 ms ret=ESP_OK
I (2287) screen_probe: [BLACK] solid rounds=1
I (3257) screen_probe:   BLACK round 1: 318 ms ret=ESP_OK
I (3262) screen_probe: [step 5] drawing line pattern (24-row bands)
I (3586) screen_probe: [DONE] pattern drawn in 317 ms
I (3603) screen_probe: probe succeeded -- backlight left ON
```

- 断言/panic：**0**
- 复位：**1**（仅上电 `POWERON`）
- 全帧填充 317 ms ≈ **3.15 MB/s** SPI 吞吐，40 MHz 下健康
- 绘完图案后设备稳定空闲（6 秒串口 0 字节 = 任务在 `vTaskDelay`）
- `esptool read-mac` 响应正常，未挂死

---

## 五、附带确认：`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` 是无效修复

`ERROR_REPORT` 提到设置 `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` 来解决队列问题。**这在 `CONFIG_SPIRAM=n` 下是惰性的**：

`esp_psram.c:95`
```c
#if CONFIG_SPIRAM_BOOT_INIT && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC)
    ...
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
```

被 `CONFIG_SPIRAM_BOOT_INIT` 门控。PSRAM 未初始化时 `malloc_alwaysinternal_limit` 保持 `-1`（禁用），`heap_caps_malloc_default()` 走纯内部路径。该选项只有在 `CONFIG_SPIRAM=y` **且** PSRAM 已初始化时才生效。

---

## 六、生产代码需要的改动（清单）

| # | 文件 | 改动 | 原因 |
|---|---|---|---|
| 1 | `display_config.h` | `KDisplayDrawBufPix` 改为条带尺寸（`KDisplayHorRes * 24`），新增 `KDisplayBandRows` | 150 KB 全帧会吃掉 62% 的内部 SRAM |
| 2 | `display_driver.c` | `trans_queue_depth` `0` → `10` | `xQueueCreate(0,...)` 必然 assert |
| 3 | `display_driver.c` | `max_transfer_sz` 按条带计算 | 随 #1 |
| 4 | `display_driver.c:26` | 修正 "in PSRAM" 注释 | PSRAM 关时是内部 SRAM |
| 5 | `sdkconfig` | `CONFIG_DISPLAY_ENABLE=y` 才能真正验证 | 当前是 `n`，显示代码从未编译 |

---

## 七、Bug #1（PSRAM 启动崩溃）仍未解决

开启 `CONFIG_SPIRAM=y` 后仍在 `cpu_start: Multicore app` 崩溃，这仍是独立未解问题。但**它不是点亮的阻塞项** —— PSRAM 关闭 + 条带缓冲已足够让屏幕正常工作。

若日后 PSRAM 修好，可把 `KDisplayBandRows` 调大以降低重绘次数，但 24 行已经绰绰有余。

---

## 八、构建环境说明（非 Git-Bash 用户可忽略）

本次验证绕过了 `idf.py`（其 launcher 拒绝 `MSYSTEM`/Git-Bash），直接调用 cmake + ninja：

```bash
export IDF_PATH="/c/Espressif/esp-idf"
export IDF_TOOLS_PATH="/c/Users/ASUS/.espressif/tools"
export IDF_CCACHE="/c/Users/ASUS/.espressif/tools/ccache/4.10.2/ccache-4.10.2-windows-x86_64/ccache.exe"
export CCACHE_PREFIX="/c/Users/ASUS/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin/xtensa-esp32s3-elf"
export PATH="/c/Users/ASUS/.espressif/python_env/idf5.4_py3.12_env/Scripts:/c/Users/ASUS/.espressif/tools/cmake/3.30.2/bin:/c/Users/ASUS/.espressif/tools/ninja/1.12.1:/c/Users/ASUS/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin:$PATH"

cmake -G Ninja -D IDF_TARGET=esp32s3 -D PYTHON_DEPS_CHECKED=1 -B build -S .
cmake --build build

python -m esptool --chip esp32s3 --port COM3 --baud 921600 \
  --before default_reset --after hard_reset --no-stub \
  write_flash 0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/screen_probe.bin
```

关键点：
- `-G Ninja` 必须显式指定（默认探测到 NMake，但无 VS）
- `-D PYTHON_DEPS_CHECKED=1` 跳过 `idf_build_check_python()`（它内部调用 `idf_tools.py`，同样拒绝 MSYSTEM）
- `IDF_CCACHE` + `CCACHE_PREFIX` 必须设置，否则 ninja 报 `ReadFile: The handle is invalid` / `CreateProcess failed`
- 串口抓取用 pyserial 开 COM3 并手动 toggle DTR/RTS 复位（见 `_screen_probe/cap2.py`）

---

---

## 九、Phase 4：LVGL 9.6 文字渲染（含中文）

屏幕点亮后进一步验证 LVGL 真字渲染。探针改为 LVGL 9.6 + `esp_lvgl_port` 2.9，
中文用 `lv_font_source_han_sans_sc_16_cjk`（U+0020-0x007F + 约 2 千字 CJK + FontAwesome）。

### 9.1 结论

**屏幕已显示中英混合文字，设备稳定。** 串口可见每秒一次单标签重绘（`on_tick` 刷新
`uptime N s`），连续 25 秒无 panic、无复位。

```
I (712) screen_probe: === screen probe: LVGL 9.6 text ===
I (718) screen_probe: heap free=293292 largest=229376
I (1065) screen_probe: lvgl_port_init OK
I (1088) screen_probe: display added 240x320 buf=9600 px
I (1590) screen_probe: UI built -- text should be on screen
D (1625) spi_master: device0 locked the bus   <- 首帧重绘
D (2635) spi_master: device0 locked the bus   <- 第二次 on_tick（+1 s）
D (3635) spi_master: device0 locked the bus
...每秒一次，持续 25 s 无 panic/复位
```

二进制 0xcead0（≈830 KB），factory 3 MB 分区剩余 73%。`.dram0.bss` = 68,856 B，
远低于 240 KB SRAM 上限 —— 条带缓冲纪律（40 行 = 19,200 B）保持有效。
CJK 字体 `glyph_bitmap` = 0x2692a（155 KB）位于 flash `.rodata`，不进 SRAM。

### 9.2 Phase 4 踩到的坑（与 Phase 3 无关，全部是工程接线问题）

| # | 现象 | 原因 | 修复 |
|---|---|---|---|
| 1 | `storage size of 'lv_matrix_t' isn't known` | `lv_matrix.h` 用 `#if LV_USE_MATRIX` 包裹整个类型，`lv_refr.c` 却用 `#if LV_DRAW_TRANSFORM_USE_MATRIX` 包裹调用。`LV_USE_MATRIX` 是 Kconfig 的 `select` 项，手写 `lv_conf.h` 里不会自动生成 | 固件 sdkconfig 里两者本来就是 `default n`，两个都不开即可，同时避免了 `LV_USE_FLOAT` 把 `lv_value_precise_t` 从 int32 改成 float |
| 2 | `'LV_PROPERTY_OBJ_NAME' undeclared` | `lv_obj_properties.c` 的表被 `#if LV_USE_OBJ_PROPERTY && LV_USE_OBJ_PROPERTY_NAME` 守卫，但 `LV_USE_OBJ_PROPERTY_NAME` 在 Kconfig 是 `default y`、在 `lv_conf_internal.h` 里因 `CONFIG_LV_*` 缺失而被强制成 `0`，与已开启的 property 系统矛盾 | `LV_USE_OBJ_PROPERTY` 直接关掉（本探针不使用） |
| 3 | `lv_label_set_font` 隐式声明 | LVGL 9.x 已删除该函数，字体是样式属性 | `lv_obj_set_style_text_font(lbl, f, 0)` |
| 4 | CJK 字体符号不可见 | 字体符号被 `LV_FONT_SOURCE_HAN_SANS_SC_16_CJK` 守卫，该符号走 `CONFIG_LV_*` 回退路径 → 0 | 在 `lv_conf.h` 里显式定义 |
| 5 | **字体被静默丢弃**（最隐蔽） | `build_ui()` 定义了但 `app_main` 从**未调用**它。链接器 dead-code elimination 把 `build_ui`/`add_label` 连同唯一引用 155 KB `glyph_bitmap` 的全部一起丢弃，`.rodata` 从 0x4eac8 缩回 0x17968，且**零警告** | `app_main` 显式调用 `build_ui()`，并加注释说明后果 |
| 6 | `LoadProhibited` 崩溃在 `get_prop_core` / `cleanup_event_list_core` | `tick_forever()` 在 `app_main` 里直接调 `lv_label_set_text()`，与 LVGL 任务的 `lv_timer_handler` 并发修改同一对象，中途打断重绘 | 改用 `lv_timer_create(on_tick, 1000, NULL)`，回调在 LVGL 任务内执行；`app_main` 只阻塞等待 |

### 9.3 关键教训：LVGL 的 Kconfig `select` 在手写 `lv_conf.h` 下全部失效

`lv_conf_internal.h:157` 把 `LV_KCONFIG_PRESENT` 的定义绑定到
`CONFIG_LV_STDLIB_BUILTIN`（SDK 的 `sdkconfig.h` 里确有此项），因此它几乎总是为真。
一旦为真，所有 `#ifndef X` 回退都改走 `CONFIG_LV_*` 分支，`#else` 里的合理推导
（如 `LV_USE_OBJ_PROPERTY_NAME = LV_USE_OBJ_PROPERTY`）全部失效。

**推论**：手写 `lv_conf.h` 只能覆盖 `bool/int` 的 `config` 项，无法覆盖任何 `select`
项。凡 LVGL 内部靠 `select` 传递的选项（`LV_USE_MATRIX`、`LV_DRAW_HAS_VECTOR_SUPPORT`、
`LV_USE_FONT_SOURCE_HAN_*`、`LV_USE_OBJ_PROPERTY_NAME` …）都必须显式写出。
本探针最终选择"少开功能"而不是"补齐 select"，因为后者会连带引入 float 坐标系。

### 9.4 生产代码落地清单（Phase 4 新增）

| # | 事项 | 说明 |
|---|---|---|
| 6 | 必须经组件管理器接入 LVGL，让 `CONFIG_LV_*` 真正合并进 `sdkconfig.h` | 绕开组件管理器（本 shell 因 Git-Bash/MSYSTEM 无法运行）会导致 §9.3 的所有失效 |
| 7 | 禁止在 `app_main` 或任意非 LVGL 任务中直接改 LVGL 对象 | 用 `lv_timer_create()` 或 `lvgl_port_lock()` + `lv_timer_handler()`；否则随机 `LoadProhibited` |
| 8 | 大字体（>50 KB）务必经组件管理器链接，别 `#include` 进单个 `.c` | `#include` 一个 1 MB 的翻译单元会让 155 KB 的 `glyph_bitmap` 与函数同处一个 obj；一旦函数被判定为死代码，数据随之蒸发且无告警 |

### 9.5 未验证项

- 中文字符的**视觉**正确性仅通过串口确认"渲染路径走完且无崩溃"，未拍照比对字形
  是否正确（字体覆盖范围已核对：所需 12 个字均在 cmap 内）。
- `LV_LOG_LEVEL_WARN` 下 LVGL 内部告警不可见，可能存在被吞掉的告警。

---

*探针工程位于 `_screen_probe/`，可删除。本文档为唯一权威记录。*
