# PLAN — ESP32-S3 兼容层

> 状态：**待审核**（审核通过后执行）

## 目标

在不破坏 ESP32-P4 支持的前提下，让固件可通过 `idf.py set-target` 在 **S3 N8R8（8MB Flash + 8MB OPI PSRAM）** 与 **P4（16MB Flash + 16MB PSRAM）** 间一键切换，并完成 S3 编译验证（烧录待购板后）。

## 已确认参数

- S3 硬件：N8R8（8MB Flash + 8MB OPI PSRAM）
- 传感器 I2C 引脚：复用 P4 引脚（SDA=18 / SCL=8）
- 兼容层形态：`firmware/main/platform/` 子目录（纯头文件）
- 验证范围：仅编译，烧录待购板后
- 优先级：S3 优先，验证通过后再考虑 P4

---

## 一、新建 `firmware/main/platform/` 兼容层

```
platform/
├── platform.h            # 统一入口：按 CONFIG_IDF_TARGET 分派
├── platform_esp32p4.h    # P4 专用常量
└── platform_esp32s3.h    # S3 专用常量
```

`platform.h` 核心逻辑：

```c
#pragma once
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
    #include "platform_esp32p4.h"
#elif CONFIG_IDF_TARGET_ESP32S3
    #include "platform_esp32s3.h"
#else
    #error "Unsupported target"
#endif
```

### 常量表

| 常量 | P4 | S3 N8R8 |
|------|----|---------|
| `KPlatformName` | `"ESP32-P4"` | `"ESP32-S3"` |
| `KPlatformCpuMaxMhz` | 400 | 240 |
| `KPlatformCpuMinMhz` | 40 | 80 |
| `KPlatformSramBytes` | 768KB | 512KB |
| `KPlatformPsramBytes` | 16MB | 8MB |
| `KPlatformArenaSize` | 1MB | 1MB |
| `KPlatformFlashSize` | 16MB | 8MB |

---

## 二、`config.h` 重构

- 移除 `KTensorArenaSize`、`KSensorBufferSize`（移入 platform 头文件）
- 顶部 `#include "platform/platform.h"`
- 保留芯片无关常量（MTU、缓存条数、采样率、I2C 引脚/频率）

## 三、`power/power_manager.c` 参数化

- `max_freq_mhz = KPlatformCpuMaxMhz`、`min_freq_mhz = KPlatformCpuMinMhz`
- 头文件 `power_manager.h` 无需改动

## 四、`main.c`

- 启动日志改用 `KPlatformName`（`esp_psram_get_size()` 运行时真实值保留）

## 五、sdkconfig 拆分（利用 ESP-IDF 原生 `<TARGET>` 合并机制）

- **`sdkconfig.defaults`**（通用）：移除 `CONFIG_IDF_TARGET` 行（由 set-target 管理）；保留 FreeRTOS / NimBLE / I2C 引脚（两目标一致）
- **`sdkconfig.defaults.esp32p4`**（新建，从现状迁入 P4 专属项）：PSRAM 120M、flash 16MB
- **`sdkconfig.defaults.esp32s3`**（新建）：OPI PSRAM 80M（`CONFIG_SPIRAM_MODE_OCT=y`）、flash 8MB（`CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`）

## 六、CMakeLists.txt

- `INCLUDE_DIRS` 增加 `"platform"`（纯头文件，无需 SRCS）

## 七、S3 编译验证

1. 安装 Xtensa 工具链：`install.ps1 esp32s3`（约 200MB 下载，本机现仅装了 RISC-V）
2. `idf.py set-target esp32s3 && idf.py build`（首次全量编译约 10-20 分钟）
3. 修复编译错误直至生成 `TianshangPulse.bin`
4. 记录二进制大小，回填 README Benchmark

## 八、文档同步（AGENTS.md §5 义务）

- `README.md` / `docs/README.zh-CN.md`：Build 章节补双目标切换命令
- `docs/MEMORY_LAYOUT.md`：新增 S3 内存行（512KB SRAM / 8MB PSRAM / 1MB arena）
- `docs/POWER_BUDGET.md`：S3 240MHz 上限说明
- `docs/HARDWARE.md`：新增 S3 N8R8 变体章节

## 九、提交推送

按既定模式 commit + push 到 `origin/main`。

---

## 范围边界

- 本轮只做兼容层 + 编译验证
- 不做烧录（待购板）
- 不改传感器 / BLE / TFLite 业务代码（其底层 API 在 P4/S3 一致，无需抽象）

## 风险与备注

- Xtensa 工具链下载约 200MB，取决于网络速度
- S3 首次全量编译耗时长（10-20 分钟）
- 若 `CONFIG_SPIRAM_MODE_OCT` 等 S3 配置项在 v5.4 中有差异，以实际 Kconfig 为准调整

---

# 修订版（审核反馈整合）

> 状态：**已审核**（补充遗漏项后执行）

## 审核意见核实结果（基于实测）

| # | 审核建议 | 实测结论 | 处置 |
|---|----------|----------|------|
| 1 | 分区表适配 | **当前项目用默认 `partitions_singleapp.csv`（factory=1MB）**，非自定义。现 bin 仅 440KB，8MB Flash 下**不超限、无烧录风险** | 降级为**增强项**：新建 8MB 分区表预留模型区（见任务 10） |
| 2 | SRAM 审计 | 全工程**无大型静态数组**（仅 `gatt_db` 表），sensor=4096 / inference=8192 栈。S3 512KB SRAM 充裕 | 补充任务栈常量进 platform 头文件，供按目标微调（见任务 11） |
| 3 | I2C 引脚 | S3 Strapping 为 **GPIO0/3/45/46**，GPIO8/18 非 Strapping，无启动冲突 | 保留 18/8，但允许 platform 头文件覆盖；列入烧录前原理图核对项（见任务 12） |
| 4 | PSRAM S3 配置 | 已确认需要 `CONFIG_SPIRAM_MODE_OCT=y` 等 | 编译时以实际 Kconfig 校准（计划已含） |
| 5 | TFLite LSTM 算子 | `esp-tflite-micro` 官方支持 Xtensa+ESP-NN；**当前 `run()` 为空、未接模型**，算子风险实际为 0 | 算子预检延后到模型落地阶段，本轮不阻塞 |

## 补充任务

### 10. 分区表适配（增强）

- 新建 `firmware/partitions_8mb.csv`（8MB Flash 布局）：

```
# Name,     Type,  SubType, Offset,   Size,    Flags
nvs,        data,  nvs,     0x9000,   0x6000,
phy_init,   data,  phy,     0xf000,   0x1000,
factory,    app,   factory, 0x10000,  0x300000,  # 3MB 固件区
model,      data,  spiffs,  0x310000, 0x100000,  # 1MB 模型存储
storage,    data,  spiffs,  0x410000, 0x3F0000,  # 剩余 ~4MB 用户数据/缓存
```

- `sdkconfig.defaults.esp32s3` 指定：

```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_8mb.csv"
```

- P4 维持默认 singleapp（16MB 下 1MB factory 够用，本轮不动）

### 11. 任务栈参数化（SRAM 安全）

- platform 头文件增加：

```c
// P4 / S3 可差异化
#define KMainTaskStackBytes       (4096)   // 保持
#define KSensorTaskStackBytes     (3072)   // S3 可降，P4=4096
#define KInferenceTaskStackBytes  (8192)   // 推理栈保持较大
```

- S3 推理任务栈保持 8192（TFLite 调用栈需求）
- 离线事件缓存已在 PSRAM（`heap_caps_malloc(MALLOC_CAP_SPIRAM)`），无需迁移
- `main.c` 中 `xTaskCreate` 改引用上述常量

### 12. I2C 引脚可覆盖

- `platform_esp32s3.h` 增加：

```c
#define KPlatformI2cSda  (18)   // 默认复用 P4
#define KPlatformI2cScl  (8)
```

- `config.h` 改用 platform 常量（替代硬编码 `CONFIG_SENSOR_SDA_GPIO`/`SCL_GPIO`）
- 烧录前需对照 S3 DevKitC-1 原理图核对 GPIO8/18 无冲突；如有冲突改此常量即可

---

## 修正后的执行顺序

1. 更新 `PLAN.md`（本修订版，即本文档）
2. 建 `platform/` 兼容层 + 改 `config.h` / `power_manager.c` / `main.c` / `CMakeLists.txt`
3. 拆 `sdkconfig.defaults`（通用 + `.esp32p4` + `.esp32s3`）+ 新增 `partitions_8mb.csv`
4. 安装 Xtensa 工具链 → `idf.py set-target esp32s3` → `idf.py build` → 修复错误 → 产出 bin
5. 文档同步（README ×2 / MEMORY_LAYOUT / POWER_BUDGET / HARDWARE）
6. commit + push 到 `origin/main`

## 预期与边界

- 首次 S3 编译预计遇到 3-5 个配置相关错误，属正常范围，1-2 小时可修完
- 本轮只做兼容层 + 编译验证，不做烧录（待购板），不改传感器/BLE/TFLite 业务代码
