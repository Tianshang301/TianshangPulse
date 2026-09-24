# AGENTS.md — TianshangPulse

> **AI 辅助开发行为规范**  
> 本文档定义了参与 TianshangPulse 项目开发的 AI Agent 角色、技术约束与协作协议。  
> 适用于：GitHub Copilot / Cursor / Claude Code / 任何接入本仓库的 AI 编程助手。

---

## 1. 项目概述

TianshangPulse 是一款基于 **ESP32-P4** (400MHz 双核 RISC-V) 的端侧 AI 智能手表固件，与 [TianshangHealth](https://github.com/Tianshang301/TianshangHealth) Android APP 构成完全离线的健康监测生态。

- **核心能力**：7 参数逻辑回归（RR 间期特征）+ 运动门控的完全离线 AF 检测，val AUC 0.928
- **技术栈**：ESP-IDF (C/C++) + FreeRTOS + LVGL（逻辑回归硬编码部署；TFLite Micro 为深度模型预留）
- **硬件**：ESP32-P4 (16MB PSRAM) + MAX30102 (PPG) + MPU6886 (加速度)
- **通信**：BLE GATT Server，与 TianshangHealth APP 双向同步

---

## 2. Agent 角色定义

### 🔧 `Firmware-Agent` — 固件开发专家

**职责范围**：`firmware/` 目录下的所有 C/C++ 代码

**核心约束**：
- 所有代码必须兼容 **ESP-IDF v5.4+** 和 **ESP32-P4** 目标
- 优先使用内部 SRAM (768KB) 存放热路径数据，PSRAM (16MB) 存放模型权重和缓存
- 禁止在 ISR (中断服务例程) 中调用堆分配 (`malloc`/`free`)
- 所有 FreeRTOS 任务必须配置明确的栈大小和优先级
- TFLite Micro 的 `arena` 必须通过 `heap_caps_malloc(MALLOC_CAP_SPIRAM)` 从 PSRAM 分配

**编码规范**：
```c
// ✅ 正确：显式内存区域分配
uint8_t *tensor_arena = (uint8_t *)heap_caps_malloc(
    kTensorArenaSize, 
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
);

// ❌ 错误：默认堆分配，可能耗尽内部 SRAM
uint8_t *tensor_arena = (uint8_t *)malloc(kTensorArenaSize);
```

**文件组织**：
```
firmware/main/
├── tflite/          # 推理引擎封装（只暴露 init/run/deinit）
├── sensors/         # 传感器驱动（统一接口：init/sample/deinit）
├── ble/             # GATT Server 实现
├── ui/              # LVGL 界面与屏幕管理
└── power/           # 功耗管理与低功耗模式
```

---

### 🤖 `ML-Agent` — 端侧模型部署专家

**职责范围**：`model/` 目录及 `scripts/` 中的模型转换流水线

**核心约束**：
- 模型必须可复现：训练脚本需固定 `random_seed`，记录完整超参数
- 量化流程：**PyTorch → ONNX → INT8 (per-channel) → TFLite Micro flatbuffer**
- 模型输入/输出张量维度必须在代码和文档中同步更新
- 每次模型更新必须附带 `benchmark.md`：PC 端延迟、模拟 ESP32-P4 延迟、精度损失

**量化 checklist**：
- [ ] 使用代表数据集（≥1000 条样本）进行 calibration
- [ ] 验证 INT8 输出与 FP32 输出的余弦相似度 > 0.95
- [ ] 确认 TFLite Micro 支持所有算子（LSTM/GRU 需检查 op resolver）
- [ ] 生成 `model_data.cc` 后验证数组大小与 `.tflite` 文件一致

**关键脚本**：
```bash
# 模型转换流水线（不可跳过任何步骤）
python scripts/convert_model.py     --input model/tianshang_lstm_fp32.onnx     --calib-data data/calib_ppg_1k.npz     --output firmware/main/tflite/model_data.cc     --verify
```

---

### 📱 `Sync-Agent` — 端云协同与协议专家

**职责范围**：BLE GATT 协议实现、与 TianshangHealth APP 的数据同步逻辑

**核心约束**：
- GATT 协议定义以 `docs/PROTOCOL.md` 为唯一真理源（Source of Truth）
- 手表端（Server）定义协议，APP 端（Client）遵循协议
- 所有特征值 UUID 必须注册在 `ble/gatt_server.c` 的 `gatt_db` 中
- 数据包格式：小端序 (Little Endian)，CRC-8 校验
- 无网场景下，异常事件必须本地缓存（循环缓冲区，PSRAM），恢复连接后批量上报

**GATT 特征值规范**（不可擅自修改 UUID）：
| UUID | 属性 | 说明 |
|------|------|------|
| `0x2A37` | Notify | 实时心率 (uint16_t, BPM) |
| `0x2A5F` | Notify | 实时血氧 (uint8_t, %) |
| `0xFFF1` | Write | APP→手表：用户配置（阈值/性别/年龄） |
| `0xFFF2` | Notify | 手表→APP：异常事件（类型 + 时间戳 + 置信度） |
| `0xFFF3` | Write | APP→手表：模型 A/B 切换指令 |
| `0xFFF4` | Read/Write | 批量数据同步（离线缓存的原始 PPG） |

> 上表为摘要，**权威定义始终是 `docs/PROTOCOL.md`**；新增/修改特征值必须走 §9 的协议变更流程（含向量与 `protocol-pin.json` 同步）。

---

### 🔌 `Hardware-Agent` — 硬件与低功耗专家

**职责范围**：`hardware/` 目录、BOM 选型、功耗预算

**核心约束**：
- 所有元器件必须是**嘉立创可采购**或**淘宝常用现货**（保证开源可复刻）
- 优先使用 I2C 接口传感器（减少 GPIO 占用）
- 功耗目标：主动监测模式 ≤ 30mA，待机模式 ≤ 500μA
- PCB 设计必须考虑 PSRAM 高速信号完整性（阻抗控制、等长走线）

**低功耗 checklist**：
- [ ] LP 核（40MHz）负责看门狗和简单中断唤醒
- [ ] HP 核（400MHz）仅在推理时全速运行，其余时间降频
- [ ] PSRAM 在空闲时进入半休眠模式（如果 ESP32-P4 支持）
- [ ] 传感器使用单次采样模式（One-shot），禁止连续高功耗模式

---

## 3. 跨 Agent 协作协议

### 3.1 模型变更流程

```
ML-Agent 训练新版模型
    ↓
ML-Agent 运行 scripts/convert_model.py → 生成 model_data.cc
    ↓
ML-Agent 提交 PR，附带 benchmark.md（延迟/精度对比）
    ↓
Firmware-Agent Review：确认 arena 大小、算子兼容性、内存布局
    ↓
Sync-Agent Review：确认模型版本号同步到 GATT 特征值 0xFFF3 的协议中
    ↓
合并到 main，Firmware-Agent 烧录验证
```

### 3.2 协议变更流程

```
Sync-Agent 修改 docs/PROTOCOL.md（Server 端定义）
    ↓
Sync-Agent 在 TianshangHealth 仓库提交对应 Issue
    ↓
双方 Review 兼容性
    ↓
Firmware-Agent 实现手表端（Server）
    ↓
TianshangHealth 团队实现 APP 端（Client）
```

### 3.3 问题上报格式

所有 Agent 发现的问题必须在 GitHub Issue 中使用以下模板：

```markdown
## Agent
`Firmware-Agent` / `ML-Agent` / `Sync-Agent` / `Hardware-Agent`

## 模块
`sensors/max30102.c` / `tflite/inference_engine.cc` / ...

## 现象
（客观描述，附日志或波形）

## 复现步骤
1. ...
2. ...

## 预期行为
...

## 实际行为
...

## 环境
- ESP-IDF 版本：
- 芯片型号：ESP32-P4NRW16 / NRW32
- 编译参数：
```

---

## 4. 代码审查红线

以下问题在 PR 中必须 blocking：

| 红线 | 说明 |
|------|------|
| **内存泄漏** | PSRAM/SRAM 分配无对应释放 |
| **栈溢出** | FreeRTOS 任务栈大小未经验证 |
| **硬编码魔法数** | 传感器阈值、UUID、缓冲区大小必须用 `#define` 或 `const` |
| **无边界检查** | 所有数组/缓冲区操作必须检查越界 |
| **协议不一致** | GATT 数据格式与 `docs/PROTOCOL.md` 不符 |
| **模型未验证** | 新模型未通过 `scripts/verify_model.py` |

---

## 5. 文档同步义务

任何代码变更如果涉及以下方面，必须同步更新文档：

| 变更类型 | 必须更新的文档 |
|---------|--------------|
| 新增传感器 | `docs/HARDWARE.md` + `docs/POWER_BUDGET.md` |
| 修改 GATT 协议 | `docs/PROTOCOL.md` + `docs/PROTOCOL_VECTORS.md` + `protocol-pin.json`（三份同步复制到 TianshangHealth）+ 本文件 §9 |
| 新增/修改特征值 | `docs/PROTOCOL.md` + `docs/PROTOCOL_VECTORS.md` + `protocol-pin.json` + 本文件 §2 的 UUID 摘要表 |
| 修改广播/连接行为 | `firmware/main/config.h` + `docs/POWER_BUDGET.md`（若影响占空比）+ `PLAN_WATCH_INTEGRATION.md` 实施状态 |
| 模型结构变更 | `docs/MODEL_ARCH.md` + `README.md` 中的 Benchmark 表格 |
| 内存布局调整 | `docs/MEMORY_LAYOUT.md` |
| 新增构建依赖 | `README.md` 的 Build 章节 |

---

## 6. 快速参考

### 6.1 构建命令

```bash
# 设置目标芯片
idf.py set-target esp32p4

# 构建
idf.py build

# 烧录并监控
idf.py flash monitor

# 仅构建模型数据
python scripts/export_lr_coefs.py --weights model/af/af_model_lr.npz
```

### 6.2 内存诊断命令

```bash
# 查看堆内存分布
idf.py monitor  # 然后在 shell 中输入: heap_caps_print_heap_info(MALLOC_CAP_DEFAULT)

# 查看任务栈使用
idf.py monitor  # uxTaskGetStackHighWaterMark()
```

### 6.3 关键常量

```c
// firmware/main/config.h
#define KTensorArenaSize        (1024 * 1024)   // 1MB PSRAM for TFLite
#define KSensorBufferSize       (1024 * 50)     // 50KB SRAM for PPG ring buffer
#define KBleMtuSize             512
#define KMaxOfflineEvents       100             // 离线缓存事件数
#define KInferenceIntervalMs    40              // 25Hz 推理频率
```

---

## 7. 外部引用

- **TianshangHealth (APP 端)**：https://github.com/Tianshang301/TianshangHealth
- **ESP-IDF 文档**：https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/
- **TFLite Micro**：https://github.com/tensorflow/tflite-micro
- **立创开源广场**：搜索 "TianshangPulse"

---

## 8. 历史计划归档

> 已执行的计划不再保留于 `PLAN.md`（该文件始终承载"当前进行中"的计划）。
> 以下为已完结计划的压缩摘要。

### 8.1 ✅ ESP32-S3 兼容层（已执行，commit 4989de6）

**目标**：固件支持 `idf.py set-target esp32s3|esp32p4` 双目标一键切换，并完成 S3 编译验证。

**已确认参数**：S3 N8R8（8MB Flash + 8MB OPI PSRAM）；I2C 引脚复用 P4（SDA=18/SCL=8）；兼容层形态 `firmware/main/platform/`（纯头文件）；仅编译、不烧录（待购板）；S3 优先验证。

**执行要点（九步）**：
1. 新建 `platform/`：`platform.h`（按 `CONFIG_IDF_TARGET` 分派）+ `platform_esp32p4.h` / `platform_esp32s3.h`。常量：CPU 频率（P4 400/40、S3 240/80）、SRAM/PSRAM/Flash、arena 1MB、任务栈、I2C 引脚
2. `config.h`：`KTensorArenaSize`/I2C 引脚改用 platform 常量，保留芯片无关项
3. `power_manager.c`：CPU 频率参数化（`KPlatformCpuMaxMhz/MinMhz`）
4. `main.c`：日志用 `KPlatformName`，任务栈用常量
5. sdkconfig 拆分：`sdkconfig.defaults`（通用）+ `.esp32p4`（PSRAM 120M / flash 16MB）+ `.esp32s3`（OPI PSRAM 80M / flash 8MB / 自定义分区表）；已移除 `CONFIG_IDF_TARGET` 行
6. `CMakeLists.txt`：`INCLUDE_DIRS` 加 `platform`；`esp_psram` 进 PRIV_REQUIRES
7. S3 验证：安装 Xtensa 工具链 → `set-target esp32s3` → build 通过（bin 0x85bf0 ≈ 548KB，factory 3MB 剩余 83%）
8. P4 回归：`set-target esp32p4` → build 通过（未破坏）
9. 文档同步：README 英/中 Build、MEMORY_LAYOUT、POWER_BUDGET、HARDWARE

**审核补充任务**：
- 10. 分区表：`firmware/partitions_8mb.csv`（nvs/phy_init/factory 3MB/model 1MB/storage ~4MB），仅 S3 使用（PSRAM 8MB 下默认 singleapp 也安全，此项为预留模型区）
- 11. 任务栈参数化：`KMain/Sensor/InferenceTaskStackBytes`（S3 sensor=3072，推理保持 8192）
- 12. I2C 引脚可覆盖：`KPlatformI2cSda/Scl`（烧录前核对 S3 DevKitC 原理图）

**风险应对**：Xtensa 工具链 ~200MB 下载；S3 首次全量编译 10-20 分钟；S3 配置项以实际 Kconfig 为准。CSV 分区表不支持行内 `#` 注释（教训：注释须独立成行）。

**后续**：S3 N8R8 开发板到位后烧录验证运行时（含 GPIO8/18 原理图核对）；P4 迁移待 S3 验证通过后按同路径推进。

---

### 8.2 ✅ P0 正确性修复 + 训练/推理对齐（阶段 1+2，已执行）

**内容**：A1 `max30102` 空指针防护 / A2 双缓冲 + 事件驱动采样 / A3 FIFO 批量 drain + OVF 检测 / A4 逐窗 Z-score+clip±3（训练-推理对齐）/ A5 `offline_cache` `peek_batch` + `pop(n)` + `0xFFF4` 精确 ACK / A6 `portMUX` 临界区 / B5 任务栈校验 + TWDT；E1 parity 硬门。
**验证**：`tests/host/test_offline_cache.c` ALL PASS；`scripts/parity_check.py` 硬门全绿；P4/S3 双目标编译通过。
**归档说明**：`PLAN.md` 保留原文并加"当前计划指向"横幅；**本轮接线计划见 `PLAN_WATCH_INTEGRATION.md`**。

### 8.3 ⏳ 与 TianshangHealth 接线（进行中）

见 [`PLAN_WATCH_INTEGRATION.md`](./PLAN_WATCH_INTEGRATION.md)（Server 侧任务卡 P1–P9；对端同名文档在 TianshangHealth 仓库）。

---

## 9. 与 TianshangHealth 接线协作规范

> 本章定义本仓库与 Android APP 仓库 [TianshangHealth](https://github.com/Tianshang301/TianshangHealth) 的**协议契约与跨仓库同步规则**。
> 计划与任务卡见 [`PLAN_WATCH_INTEGRATION.md`](./PLAN_WATCH_INTEGRATION.md)。本章只写**规则**，不写**数值**。

### 9.1 角色与真源

| 项 | 内容 |
| --- | --- |
| 角色 | TianshangPulse = **GATT Server**（协议**定义方**，跨仓库变更中**先改**） |
| 协议真源 | **本仓库** `docs/PROTOCOL.md`（Health 侧为只读镜像） |
| 契约数值真源 | **本仓库** `docs/PROTOCOL_VECTORS.md`（Health 侧为只读镜像） |
| 版本/哈希真源 | `protocol-pin.json`（本仓库**生成**，两仓库必须 **byte-identical**） |
| 计划文档 | [`PLAN_WATCH_INTEGRATION.md`](./PLAN_WATCH_INTEGRATION.md)（与对端**同名**） |

**四个同步点**：`protocol-pin.json`、`docs/PROTOCOL.md`、`docs/PROTOCOL_VECTORS.md`、两仓库 `AGENTS.md` 的接线章节。
**数值唯一性**：SHA-256 哈希只允许出现在 `protocol-pin.json`；本文件（及任何 `.md`）不得复制版本号或哈希。
**生成方向不可逆**：哈希只在本仓库重算，然后整体复制到 Health；**禁止**在 Health 侧重算后回填。

### 9.2 协议变更流程（六步，顺序不可颠倒；本仓库执行第 1–4 步）

```
Pulse 改 PROTOCOL.md → Pulse 更新 PROTOCOL_VECTORS.md → Pulse 重算 SHA-256 写 pin
   → 复制三个文件到 Health（byte 级） → 双端实现 + 跑契约测试 → 双端更新文档
```

- 版本规则：**MINOR** = 兼容新增（如新增特征值）/ **MAJOR** = 改字段语义、长度、字节序 / **PATCH** = 裁决与措辞
- 新增向量**不得**修改既有向量的期望值
- 校验命令见 `PLAN_WATCH_INTEGRATION.md` §7.4（期望输出 `pin identical: True` + 四行 `True`）
- 发现协议矛盾（文档 vs 代码）时：以 `docs/PROTOCOL_VECTORS.md` 为唯一裁决依据，先登记 `protocol-pin.json` → `open_items`，再暂停实现并上报
- **提交可见性自检**：本仓库不忽略 `*.md` / `*.json`，提交协议变更时 `git status` 必须能看到 `docs/PROTOCOL_VECTORS.md` 与 `protocol-pin.json`；若看不到，说明被误加入忽略规则，须立即排查（对照 `TianshangHealth/.gitignore` 的 `*.md`/`*.json` 规则与该仓库计划文档 §7.7）

### 9.3 协议实现约束（改 GATT / 广播前必读）

| # | 约束 |
| --- | --- |
| 1 | **CRC-8**：poly `0x07` / init `0x00` / 无反射 / 无最终异或；覆盖"除末尾 CRC 字节外的全部字节"。实现必须与 `docs/PROTOCOL_VECTORS.md` 逐字节一致 |
| 2 | **整批 CRC 不可用**：对完整载荷（含其自身 CRC 字节）恒有 `crc8(msg) == 0x00`；`0xFFF4` 只发 `N × 11` 字节，**不追加**整批 CRC |
| 3 | `0xFFF1` 必须要求 `len >= 8` 且 CRC 覆盖前 7 字节（含 1 字节 reserved）；`0xFFF3` `idx > 1` 必须拒绝 |
| 4 | `0xFFF4` 必须保持 **Read 只预览、Write 才精确 pop** 的语义（`s_batch_pending`），否则 MTU 截断会永久丢事件 |
| 5 | **时间戳**必须是 Unix epoch ms；未校准时 `0xFFF2` 的 `type` 最高位置 `0x80` 表示"未校准"；**禁止**在未校准时伪装成 epoch 值 |
| 6 | **广播载荷必须显式配置**（`ble_gap_adv_set_fields` / `ble_gap_adv_rsp_set_fields`）：至少包含 flags + 自定义服务 UUID；设备名放 adv 或 scan response。**必须**检查返回值并打日志 |
| 7 | 广播间隔不得永久使用 FAST；应 FAST burst 后切 SLOW（功耗预算见 `docs/POWER_BUDGET.md`） |
| 8 | 所有 UUID、广播字段、缓冲区大小必须用 `#define`/`const`（AGENTS.md §4 红线，禁魔法数） |
| 9 | 新增特征值必须同步更新：`gatt_db`、`docs/PROTOCOL.md`、`docs/PROTOCOL_VECTORS.md`、`protocol-pin.json`、本文件 §2 的 UUID 摘要表 |
| 10 | 空口链路当前**未加密、不配对**（未配置 Security Manager）；如需加密须按协议变更流程升版并同步 Health |

### 9.4 当前实施状态

| 项 | 状态 |
| --- | --- |
| 计划 | [`PLAN_WATCH_INTEGRATION.md`](./PLAN_WATCH_INTEGRATION.md)（任务卡 P1–P9，**待用户拍板 D1–D6 / 范围 / 实机**） |
| 断点 | 已识别 11 项；P0 = B1 广播载荷未配置、B2 时间戳为开机 ms、B3 `PROTOCOL.md` §6 与实现矛盾 |
| 已正确可复用（勿改坏） | `ble_crc8`、11 字节事件打包、`0xFFF1` 8 字节校验、`0xFFF4` peek/ACK-pop、`offline_cache` 临界区、GAP 电源模式联动、双目标编译 |
| 已知明文 | 空口未加密（B5/D3），文档需显式声明 |
| 无 CI | 本仓库无 `.github/`（见任务卡 P8） |

### 9.5 任务完成门槛（DoD）

1. `scripts/run_host_tests.ps1`（含 `tests/host/test_protocol.c`）全 PASS，期望值与向量逐字节一致
2. 双目标 `idf.py build`（`esp32p4` / `esp32s3`）通过
3. 协议变更走完 §9.2 六步；`protocol-pin.json` 与 Health 侧 byte-identical
4. 未破坏 `offline_cache` / parity 既有测试；未引入任何网络能力
5. 无实机时**必须**标注"未在实机验证"，**禁止**声称已通过

---

*最后更新：2026-09-20*  
*维护者：Tianshang301*  
*协议：MIT*
