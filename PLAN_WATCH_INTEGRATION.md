# PLAN_WATCH_INTEGRATION — 手表接线计划 · Pulse 侧（GATT Server）

> **状态**：📋 待用户拍板（D1–D6 / 范围 / 实机）→ 之后由独立 Agent 按任务卡执行
> **角色**：TianshangPulse = **GATT Server**（协议定义方，**先改**）；TianshangHealth = GATT Client（遵循方，后跟）
> **对端同名文档**：`TianshangHealth/PLAN_WATCH_INTEGRATION.md`
> **协议真源（本仓库）**：`docs/PROTOCOL.md` · **数值真源（本仓库）**：`docs/PROTOCOL_VECTORS.md` · **版本锁**：`protocol-pin.json`（与 Health 侧 byte-identical）
> **取代**：`PLAN_TianshangPulse.md`（2026-09-08 初始规划，保留为历史；其「本轮 APP 开发无需固件改动」的判断**已被证伪**——见 §2）

---

## 0. 使用方式（给独立 Agent 的执行约定）

1. **先读**：`protocol-pin.json` → `docs/PROTOCOL.md` → `docs/PROTOCOL_VECTORS.md` → 本文件 §2。不要凭记忆改 GATT。
2. **只改本仓库**：所有改动限定在 TianshangPulse 内。协议变更由**本仓库先落地**，然后才轮到 Health。
3. **协议变更顺序**（违反即回滚）：改 `PROTOCOL.md` → 升版本 → 更新 `PROTOCOL_VECTORS.md` → 重算 SHA-256 写 `protocol-pin.json` → 复制到 Health（见 §7）。
4. **一次一张任务卡**，每卡有硬性验收（host 测试 / 双目标编译），未通过不得进入下一卡。
5. **红线**（AGENTS.md §4）：禁止 ISR 内堆分配、禁止无边界检查、禁硬编码魔法数（UUID/阈值/缓冲区必须 `#define` 或 `const`）、协议不一致属 blocking、任务栈必须验证。
6. **内存纪律**：热路径数据放内部 SRAM，模型权重与缓存放 PSRAM（`heap_caps_malloc(MALLOC_CAP_SPIRAM)`）。
7. **不新增任何网络能力**：本项目永久离线（无 WiFi/云）；BLE 是唯一对外接口。

---

## 1. 已核实的现状（固件代码事实）

| 层 | 现状 | 证据 |
| --- | --- | --- |
| GATT DB | 两个服务：`0x180D`（`0x2A37` N / `0x2A5F` N）+ `0xFFF0`（`0xFFF1` W / `0xFFF2` N / `0xFFF3` W / `0xFFF4` R+W），6 个特征值全部注册 | `gatt_server.c:66-115` |
| CRC-8 | `ble_crc8()`：poly `0x07` / init `0x00` / 无反射 / 无最终异或；与 Health 侧 `ProtocolParser.crc8` **逐位等价**（已用编译执行交叉验证） | `gatt_server.c:39-50` |
| 事件打包 | `ble_pack_anomaly()`：`type[1] + ts[8 LE] + conf[1] + crc8[1]` = 11 字节，CRC 覆盖前 10 字节 | `gatt_server.c:52-64` |
| 用户配置 | `0xFFF1` Write：要求 `len >= 8`，CRC 覆盖**前 7 字节**（含 1 字节 reserved），解析 `hr[2]+spo2[1]+gender[1]+age[2]` | `gatt_server.c:226-248` |
| 模型切换 | `0xFFF3` Write：`len >= 1`，`idx > 1` 拒绝（`ATT_INVALID_PDU`） | `gatt_server.c:251-263` |
| 离线批量 | `0xFFF4` Read：`offline_cache_peek_batch` 预览 ≤20 条 → 逐条 `ble_pack_anomaly` 追加到 mbuf（MTU 满即截断），记录 `s_batch_pending`；**不追加整批 CRC**；Write（任意长度）→ `offline_cache_pop(s_batch_pending)` 精确清除 | `gatt_server.c:266-298` |
| 离线缓存 | PSRAM 环形缓冲 100 条 + `portMUX` 临界区（A5/A6 已修）+ host 单测 | `ble/offline_cache.{c,h}`、`tests/host/test_offline_cache.c` |
| 广播 | `start_advertising()`：UND 模式 + **FAST** 广播间隔 + `BLE_HS_FOREVER`；**未调用** `ble_gap_adv_set_fields` / `ble_gap_adv_rsp_set_fields`（全固件 0 处 `uuids16`） | `gatt_server.c:133-146` |
| 连接事件 | GAP 事件：CONNECT → `POWER_MODE_ACTIVE`；DISCONNECT → `POWER_MODE_LIGHT_SLEEP` + 重新广播；ADV_COMPLETE → 重新广播 | `gatt_server.c:148-176` |
| 时间戳 | `esp_timer_get_time() / 1000` → **开机以来毫秒**，不是 Unix epoch ms（违反 `PROTOCOL.md` §4） | `main.c:190` |
| 通知发送 | `ble_notify_heart_rate` / `ble_notify_spo2` / `ble_notify_anomaly`：未连接返回 `ESP_ERR_INVALID_STATE` | `gatt_server.c:189-223` |
| 设备名 | `ble_svc_gap_device_name_set("TianshangPulse")`（仅设置 GAP 特征值，**不自动写入广播载荷**） | `gatt_server.c:119` |
| 协议文档 | `docs/PROTOCOL.md` 与实现存在 1 处**实质矛盾**（§6「末尾带 CRC-8」） | 见 B3 / O-1 |
| host 测试 | `tests/host/test_offline_cache.c`（+ FreeRTOS/esp_log 桩）已存在；`tests/parity/` 有 parity harness；**无协议契约测试** | `tests/` |
| 构建 | 双目标（`esp32p4` / `esp32s3`）可编译；`.gitattributes` 强制 `*.md eol=lf`、`*.c/*.h eol=lf` | `.gitattributes` |

**已核实「不要改坏」的既有正确资产**：

| ID | 资产 | 说明 |
| --- | --- | --- |
| C1 | `ble_crc8` 实现 | 与向量 V1 系列完全一致（编译执行验证过），**禁止重写** |
| C2 | 11 字节事件打包 | 与 V2–V5b 一致 |
| C3 | `0xFFF1` 的 8 字节 + CRC 覆盖 7 字节 | 与 V6 系列一致 |
| C4 | `0xFFF4` 的 peek/ACK-pop 语义 | 唯一正确做法（避免 MTU 截断丢事件），**必须保留** |
| C5 | `offline_cache` 的临界区与 ring | 已修并有 host 单测 |
| C6 | GAP 事件的电源模式联动 | 连接即全速、断开即浅睡 |
| C7 | 双目标编译能力 | S3/P4 兼容层（`platform/`）不得破坏 |

## 2. 断点清单（Server 侧；逐条给出证据）

> **修订说明**：`PLAN_TianshangPulse.md` 曾判断"本轮 APP 开发无需固件改动，仅需配合验证"——**该判断错误**。实测代码显示至少 3 个 P0 级固件侧缺陷（广播、时间戳、文档矛盾），不修则 APP 永远连不上或时间全错。

### 2.1 P0（阻塞连通 / 数据不可用）

| ID | 症状 | 证据 | 对应协议项 |
| --- | --- | --- | --- |
| **B1** | **广播载荷未配置**：`start_advertising()` 只填了 `conn_mode/disc_mode/itvl`，**从未**调用 `ble_gap_adv_set_fields` / `ble_gap_adv_rsp_set_fields`，全固件 0 处 `uuids16` → APP 侧按 `0xFFF0` 做**硬件级** ScanFilter（`BleManager.kt:93-95`）必然扫不到 | `gatt_server.c:133-146` | O-4 |
| **B2** | **时间戳语义违规**：发 `esp_timer_get_time()/1000`（开机以来 ms），而 `PROTOCOL.md` §4 要求 Unix epoch ms。ESP32-P4 无电池 RTC，只能由 APP 授时 | `main.c:190` | §4 / O-2 |
| **B3** | **文档与实现矛盾**（已证）：`PROTOCOL.md` §6 说批量"末尾带 CRC-8"，实现不追加；且整批 CRC 恒为 `0x00`（初值 0 + 无最终异或 + 无反射时 `crc8(msg‖crc8(msg)) ≡ 0`，5 组样本实测全 0）→ 该描述**必须删除**，否则 Health 侧会实现一个恒为 0 的校验 | `docs/PROTOCOL.md:56-57` ↔ `gatt_server.c:266-298` | §6 / O-1 |

### 2.2 P1（功能不完整 / 安全 / 功耗）

| ID | 症状 | 证据 | 归属 |
| --- | --- | --- | --- |
| **B4** | **用户配置与模型索引不持久化**：`s_user_config` / `s_model_index` 仅为静态变量；`main.c:48` 虽已 `nvs_flash_init()`，但 GATT 侧**未使用 NVS** → 掉电重启后配置丢失（APP 每次重连需重下发） | `gatt_server.c:28-29`、`main.c:48-51` | D1/D4 之外的独立项 |
| **B5** | **链路未加密、不配对（建议按 P0 处理）**（已核实）：`gatt_server.c:184` 仅设置 `ble_hs_cfg.sync_cb`，**没有**任何 `sm_io_cap` / `sm_bonding` / `sm_sc` / `sm_mitm` 配置 → **任意邻近 BLE 设备都能直接连上**：读 HR/血氧、Read `0xFFF4` 拉走全部离线 AF 事件、Write ACK 清空缓存、改 `0xFFF1` 阈值 | `gatt_server.c:178-187` | 安全项（D3，**建议本期修**） |
| **B6** | **广播功耗**：使用 `BLE_GAP_ADV_FAST_INTERVAL1_MIN/MAX` + `BLE_HS_FOREVER` 永久快速广播（约 20–30ms 间隔） → 空闲功耗偏高，与 `POWER_BUDGET.md` 目标相悖 | `gatt_server.c:135-142` | HW/功耗 |
| **B7** | `0xFFF4` 的 `s_batch_pending` 是**全局单连接**状态：APP 连续 Read 两次而不 ACK 会重复交付（设计如此，去重责任在 Client）；且若 Read 后断连，事件不会被清除（正确，但需文档说明） | `gatt_server.c:31-32`、`:266-298` | 文档化即可 |

### 2.3 P2（工程卫生）

| ID | 症状 | 证据/说明 |
| --- | --- | --- |
| **B8** | 离线缓存仅 PSRAM 内存环形缓冲，掉电即丢（已知限制，`PLAN.md` 的"STANDBY 前缓存持久化"仍未做） | `ble/offline_cache.c` |
| **B9** | **无协议契约测试**：`tests/host/` 只有 offline_cache；协议纯函数（`ble_crc8`/`ble_pack_anomaly`）零测试 | `tests/host/`；AGENTS.md §4「协议不一致」是 blocking 红线却无门禁 |
| **B10** | 广播**无 scan response**：名称（13 字符）+ 服务 UUID 全放广播包会挤占空间，应拆包（adv：flags + uuids16；rsp：Complete Local Name） | `gatt_server.c:133-146` |
| **B11** | **本仓库无 CI**（无 `.github/` 目录）→ 双目标编译与 host 测试无自动门禁 | 目录清单已核实 |

### 2.4 待核对项（实施第一步必须实测，不得假设）

| ID | 内容 | 方法 |
| --- | --- | --- |
| S1 | ✅ **已核实（2026-09-20）**：`firmware/sdkconfig.defaults:12` = `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512`、`:13` = `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`（与 `config.h` 的 `KBleMtuSize 512` 一致）；BLE 侧**无任何 `sm_*` 安全配置**，且**未设 `CONFIG_BT_NIMBLE_NVS_PERSIST`** → P10 需显式开启 | 已核实，无需再查 |
| S2 | **实测广播载荷实际内容**（未调用 `set_fields` 时 NimBLE 可能广播空载荷 → 连设备名都不出现，Health 的名称匹配同样失效；也可能自动带名） | 实机 + nRF Connect；与 Health 侧 S5 是同一次实测 |
| S3 | 基线：双目标 `idf.py build` 是否通过、`tests/host` 如何编译运行（本机无 gcc/MSVC，历史用 `python -m ziglang cc`） | `scripts/` 与 `PLAN.md` 记录 |
| S4 | `POWER_BUDGET.md` 中广播相关功耗预算数值（B6 的量化依据） | 读 `docs/POWER_BUDGET.md` |

## 3. 目标架构（Server 侧）

### 3.1 组件图

```
                    TianshangPulse (GATT Server / NimBLE)
 ┌────────────────────────────────────────────────────────────────────────────────┐
 │ main.c                                                                         │
 │  · 采样/推理任务（不变）                                                        │
 │  · 异常事件时间戳：esp_timer_get_time()/1000  ⇒  time_sync_now_ms() ★（B2）     │
 │                                    │                                           │
 │                                    ▼                                           │
 │ ble/                                                                           │
 │  ├ protocol.{c,h}      ★ 新：纯函数（ble_crc8 / ble_pack_anomaly /             │
 │  │                            ble_parse_user_config / ble_build_user_config）   │
 │  │                            ⇒ 可 host 编译 + 被契约测试断言（B9）              │
 │  ├ time_sync.{c,h}     ★ 新：epoch 基准（set_epoch / now_ms / is_calibrated）    │
 │  ├ adv.{c,h}           ★ 新：广播字段与间隔策略（B1/B6/B10）                     │
 │  ├ gatt_server.c       · GATT DB（6→7 个特征值）、notify、0xFFF4 peek/ACK-pop   │
 │  └ offline_cache.{c,h} · PSRAM ring（保持不动，已修）                            │
 │                                    │                                           │
 │ 0xFFF0 服务                        ▼                                           │
 │  0xFFF1 W  用户配置(8B)      ┌─ NVS（nvs_set_blob）★ 可选：配置持久化（B4）      │
 │  0xFFF2 N  异常事件(11B)     │                                                 │
 │  0xFFF3 W  模型切换(2B)      └─ 掉电不丢                                       │
 │  0xFFF4 RW 离线批量(N×11B)                                                      │
 │  0xFFF5 W  时间同步(9B)  ★ 新增（D1，需先升协议版本）                             │
 └────────────────────────────────────────────────────────────────────────────────┘
        ▲ 广播：flags + uuids16(0xFFF0,0x180D) + rsp: 设备名      ← 修复点（B1）
        │        fast 30s → slow 间隔                            ← 修复点（B6）
   BLE 链路（未加密，见 B5/D3）
        │
   TianshangHealth（GATT Client）
```

### 3.2 关键结构性设计

| # | 设计 | 目的 | 对应断点 |
| --- | --- | --- | --- |
| 1 | `ble/protocol.{c,h}`：把 `ble_crc8` / `ble_pack_anomaly` 以及新增的 `ble_parse_user_config` / `ble_build_user_config` 抽成**不依赖 NimBLE 的纯函数**；`gatt_server.c` 改为调用它们 | 让协议实现能被 host 编译并逐条断言向量；避免"文档说的和代码做的不一样" | B9、B3 |
| 2 | `ble/time_sync.{c,h}`：`time_sync_set_epoch(uint64)` / `time_sync_now_ms()` / `time_sync_is_calibrated()`；`main.c` 与 `gatt_server.c` 统一走它 | 一处修复时间语义，避免散落的 `esp_timer_get_time()` | B2 |
| 3 | 广播字段集中在 `config.h`：`KAdvDeviceName`、`KAdvUuids16[]`、`KAdvFastBurstMs`；`adv.c` 负责 adv/rsp 两包拆分与间隔策略 | 满足 AGENTS §4「UUID/缓冲区必须用 `#define` 或 `const`」 | B1、B6、B10 |
| 4 | `0xFFF5` 加入 `gatt_db`（`BLE_GATT_CHR_F_WRITE \| BLE_GATT_CHR_F_READ`：Write 授时、Read 回读 `time_sync_now_ms()` 供 APP 校正偏移），`s_user_config` 与模型索引可存 NVS | 时间同步**可被校验** + 配置持久化 | B2、B4 |
| 5 | 未校准标志（D2）：`ble_pack_anomaly` 支持 `type \| 0x80`，由 `time_sync_is_calibrated()` 决定 | 让 Client 能区分"真实时间/开机时间"，无需加字节 | B2/O-2 |
| 6 | `tests/host/test_protocol.c` + `scripts/run_host_tests.ps1`：host 编译 `ble/protocol.c` 并断言 V1–V10f（含 P6a/P6b/P7 负例） | 契约测试与 Health 侧同组向量 | B9 |
| 7 | `.github/workflows/firmware-ci.yml`：host 测试（gcc）+ 双目标 `idf.py build`（`espressif/idf` 镜像） | 无 CI → 有 CI | B11 |
| 8 | **Security Manager 最小绑定**（Just Works + bonding + `REPEAT_PAIRING` + `CONFIG_BT_NIMBLE_NVS_PERSIST=y`） | 阻止陌生人连接并把数据取走 | B5 / D3 |

### 3.3 文件变更地图

| 动作 | 文件 | 任务卡 |
| --- | --- | --- |
| 新增 | `firmware/main/ble/protocol.c` / `protocol.h` | P4 |
| 新增 | `firmware/main/ble/time_sync.c` / `time_sync.h` | P2 |
| 新增 | `firmware/main/ble/adv.c` / `adv.h` | P1、P6 |
| 新增 | `tests/host/test_protocol.c`、`scripts/run_host_tests.ps1` | P4 |
| 新增 | `.github/workflows/firmware-ci.yml` | P8 |
| 修改 | `firmware/main/ble/gatt_server.c`（0xFFF5 **读写**、广播改为调 `adv.c`、协议函数改调 `protocol.c`、**Security Manager 配置**、可选 NVS） | P1–P6、P10 |
| 修改 | `firmware/main/main.c`（时间戳改调 `time_sync_now_ms()`） | P2 |
| 修改 | `firmware/main/config.h`（广播字段与间隔常量） | P1、P6 |
| 修改 | `firmware/main/CMakeLists.txt`（新增源文件） | P1、P2、P4 |
| 修改 | `firmware/sdkconfig.defaults`（`CONFIG_BT_NIMBLE_NVS_PERSIST=y`，P10 绑定持久化的前提） | P10 |
| 修改 | `docs/PROTOCOL.md`（§6 裁决 + `0xFFF5` 新章节 + 版本升位） | P2、P3 |
| 修改 | `docs/PROTOCOL_VECTORS.md`（新增 `0xFFF5` 向量；`0xFFF2` 未校准标志向量） | P2 |
| 修改 | `protocol-pin.json`（版本、哈希、`planned_characteristics` → `services`） | P2、P9 |
| 修改 | `AGENTS.md`（§5 文档同步表 + 新增接线章节 + §8 归档） | P9 |
| 修改 | `README.md` / `docs/HARDWARE.md` / `docs/POWER_BUDGET.md`（互链与广播功耗） | P7 |

---

## 4. 设计决策 D1–D6（**待用户拍板**；Server 侧视角，取值必须与 Health 侧完全一致）

| ID | Server 侧的落地动作 | 推荐 | Health 侧依赖 |
| --- | --- | --- | --- |
| **D1** | 新增 `0xFFF5`（**Write + Read**，9 字节：`uint64 epoch_ms` + `crc8`）；Write 后 `time_sync_set_epoch()`；**Read 返回当前 `time_sync_now_ms()` 供 APP 回读校验**；`docs/PROTOCOL.md` 升版并新增 §7 章节；新增向量（回读与 Write 同格式，复用同一组向量） | **做（含回读）** | Health T4 依赖它才有正确时间；**且只有回读才能证明同步真的生效** |
| **D2** | `ble_pack_anomaly` 在未校准时置 `type` 最高位（`\| 0x80`）；文档写明该位语义 | **做** | Health T4 解析时掩码 |
| **D3** | ⚠️ **改建议：本期做"最小绑定"**——`ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT`、`sm_bonding = 1`、`sm_sc = 1`（Just Works，配对无感）；**必须同批处理 `BLE_GAP_EVENT_REPEAT_PAIRING`**（换机/删绑定后能重新配对）与 `CONFIG_BT_NIMBLE_NVS_PERSIST=y`（绑定持久化）；未配对设备的连接直接断开 | **本期做（由"不做"上调）** | Health **T5b** 同期 `createBond()`；不做的后果：任意邻近 BLE 设备可连上读 HR/血氧、Read `0xFFF4` 拉走全部离线 AF 事件、Write ACK 清空缓存、改 `0xFFF1` 阈值 |
| **D4** | 本期 Server 侧**无需改动**（`0xFFF6` 体征上传不在范围内；`0x2A37`/`0x2A5F` 已能提供数据）。⚠️ 已核实固件**无体温通道**（MAX30102 仅 PPG，`TEMP` 寄存器全固件 0 处使用）→ 方案明确**排除体温** | **Health 侧单向做** | Health T9（收敛为仅 `vitals_samples` + HR/SPO2） |
| **D5** | 无（Server 侧不涉及 Gradle 模块） | — | Health T6 |
| **D6** | 无（Server 侧无 Room） | — | Health T9 |

> **协议版本升级的判定**：D1（新增特征值）与 D2（复用空闲标志位）都是**向后兼容**的 → **MINOR** 升级（提案：`1.0.0` → `1.1.0`，**最终以 `protocol-pin.json` 为准**）；旧固件/旧 APP 仍可各自正常工作（旧 APP 不写 `0xFFF5` → 固件保持未校准态；旧固件不发 `0xFFF2` 最高位 → 新 APP 按量级兜底）。

## 5. 任务卡（按序执行，一卡一验收）

### P1 — 广播载荷修复（0.5 人日，P0 / B1）

- **前置**：S2 实测确认当前广播载荷
- **实现要点**
  - 新增 `ble/adv.{c,h}`：`adv_init()` / `adv_start(bool fast_burst)` / `adv_stop()`
  - 新增常量到 `config.h`：`KAdvDeviceName`、`KAdvUuids16[]`（`0xFFF0`、`0x180D`）、`KAdvFastBurstMs`（30000）
  - `ble_gap_adv_set_fields()`：`flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP`；`uuids16 = {0xFFF0, 0x180D}` 且 `uuids16_is_complete = 1`；`name_is_complete = 1`
  - `ble_gap_adv_rsp_set_fields()`：放设备名（若 adv 包空间不足，名称整体移到 rsp）
  - **空间核算**：flags 3B + UUIDs16 6B + 名称（"TianshangPulse" = 13 字符 → 15B）= 24B ≤ 31B → 可全放 adv；仍建议 rsp 也带名称，兼容只抓 rsp 的客户端
  - `start_advertising()` 改为调用 `adv_start()`；DISCONNECT / ADV_COMPLETE 后重启广播（保持既有行为）
  - **必须**检查 `ble_gap_adv_set_fields` / `rsp_set_fields` 的返回值并 `ESP_LOGE` 上报
- **验收**：P4/P8 双目标 `idf.py build` 通过；`idf.py monitor` 无 adv 配置报错；实机 nRF Connect 可见 `0xFFF0` 与设备名（无实机 → 标记"未验证"）
- **禁止**：不改 GATT DB、不改任何 UUID、不把 128 位 UUID 写进广播

### P2 — `0xFFF5` 时间同步 + epoch 语义（1.0 人日，P0 / B2，依赖 D1/D2 批准）

- **前置**：P1；用户批准 D1/D2
- **执行顺序（先文档、后代码，不可颠倒）**
  1. `docs/PROTOCOL.md`：§2 表新增 `0xFFF5`（**Write + Read** / APP↔Watch / 9 字节：写授时、读回读 `now_ms`）；新增 §7「时间同步」（**必须写明：Read 返回与 Write 同格式的 9 字节 = `time_sync_now_ms()`**，供 APP 做回读校验）；§4 明确 `type` 最高位 = 未校准；顶部 `protocol_version` 升级为 MINOR（1.0.0 → 1.1.0）
  2. `docs/PROTOCOL_VECTORS.md`：新增向量（下列值已用固件同款 CRC-8 实现**预生成**；P2 落地后必须用 host 测试实际输出复核后再冻结）
     - `V11`：`0xFFF5`，`epoch_ms = 1750000000000` → `00 dc 20 74 97 01 00 00 f6`
     - `V11c`：`0xFFF5`，`epoch_ms = 1700000000000` → `00 68 e5 cf 8b 01 00 00 9d`
     - `V11d`：`0xFFF5`，`epoch_ms = 0` → `00 00 00 00 00 00 00 00 00`（**trap**：CRC 合法值为 `00`，不得当作错误）
     - `V12`：`0xFFF5` 坏 CRC（`V11` 末字节取反）→ `00 dc 20 74 97 01 00 00 09` → 设备必须**拒绝并保持时钟不变**
     - `V11b`：`0xFFF2` 未校准样本（`type=0x83`、`ts=12345` 开机 ms、`conf=88`）→ `83 39 30 00 00 00 00 00 00 58 d2`；两端断言 `typeCode=3`、`uncalibrated=true`
     - **回读**（Read `0xFFF5`）返回与 Write **同格式**的 9 字节（`uint64 now_ms` + `crc8`）→ **复用 V11 / V11c / V11d**，无需新增向量
  3. `protocol-pin.json`：`protocol_version` = `1.1.0`；重算两个 SHA-256；把 `planned_characteristics.0xFFF5` 移入 `services`
  4. 代码：
     - 新增 `ble/time_sync.{c,h}`：`time_sync_set_epoch(uint64_t)` / `time_sync_now_ms()` / `time_sync_is_calibrated()`
     - `main.c:190` 改为 `time_sync_now_ms()`
     - `ble/protocol.c` 的 `ble_pack_anomaly` 支持未校准标志（`type | 0x80`）
     - `gatt_db` 新增 `0xFFF5`（`BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ`；`access_cb` 对 **Write** 校验 `len == 9` + CRC 后调用 `time_sync_set_epoch`，对 **Read** 返回 `time_sync_now_ms()` 打包成的 9 字节）
- **验收**：`tests/host/test_protocol.c` 断言新向量；双目标编译通过；实机：写 `0xFFF5` 后**立即回读**，返回值与写入值偏差 < 5s（回读校验通过），其后 `0xFFF2` 时间戳为真实时间；APP 侧出现 `clock offset = … ms` 日志
- **禁止**：不得改 `0xFFF1`–`0xFFF4` 的任何字节布局；不得用"APP 猜时间"替代授时

### P3 — `PROTOCOL.md` §6 裁决（0.25 人日，P0 / B3）

- **动作**：按 O-1 默认方案，把 §6 的 Read 描述改为「载荷 = N × 11 字节，每条自带 CRC-8，`N = len / 11 <= 20`」，删除"末尾带 CRC-8"；补一句工程依据「整批 CRC 恒为 `0x00`（初值 0 + 无最终异或 + 无反射），故不采用」；更新 `protocol-pin.json` 哈希；把两个文件复制到 Health
- **验收**：`PROTOCOL.md` 与 `gatt_server.c` 行为一致；Health 侧镜像哈希与 pin 一致
- **禁止**：不得改代码去"迎合"旧文档描述

### P4 — 协议纯函数抽出 + host 契约测试（0.75 人日，P1 / B9）

- **前置**：P3
- **动作**
  - 抽 `firmware/main/ble/protocol.{c,h}`：`ble_crc8` / `ble_pack_anomaly` / `ble_build_user_config` / `ble_parse_user_config`（**不依赖 NimBLE 头**，只依赖 `<stdint.h>`/`<string.h>`）
  - `gatt_server.c` 改为调用这些函数（行为不变）
  - 新增 `tests/host/test_protocol.c`：逐条断言 V1–V10f + P6a/P6b/P7 负例（期望值必须与 `docs/PROTOCOL_VECTORS.md` **逐字节一致**）
  - 新增 `scripts/run_host_tests.ps1`（编译器自动探测：gcc → clang → `python -m ziglang cc`）与 `.sh` 版
  - `firmware/main/CMakeLists.txt` 增 `ble/protocol.c`、`ble/time_sync.c`、`ble/adv.c`
- **验收**：host 测试 ALL PASS；`tests/host/test_offline_cache.c` 仍 PASS（无回归）；双目标编译通过；测试条数 = 向量条数
- **禁止**：不得把 NimBLE/IDF 头引入 `protocol.c`（否则 host 编译失败）；不得跳过负例

### P5 — 配置持久化（NVS）（0.5 人日，P1 / B4，**可选卡**）

- **动作**：`s_user_config` 与 `s_model_index` 用 `nvs_set_blob` 存 `TIANSHANG_NVS_NS`/`"ble_cfg"`；`adv_init` 之前加载并 `ESP_LOGI` 回显；写入失败仅告警不阻断
- **验收**：双目标编译；实机下电重启后日志回显上次配置
- **禁止**：不得在 Notify 热路径做 NVS 写（仅配置变更时写）

### P6 — 广播间隔与功耗策略（0.5 人日，P1 / B6、B10）

- **动作**：默认 **SLOW** 间隔（`BLE_GAP_ADV_SLOW_INTVL_*`）；`adv_start(fast_burst=true)` 时用 FAST，30s 后自动切回 SLOW（用 `esp_timer` 单次回调，**不得**用阻塞 delay）；断开重连后给一次 fast burst
- **验收**：双目标编译；实机电流对比（有功耗仪时）或至少 `power_manager` 日志确认已切回 SLOW
- **禁止**：不得在 GAP 回调里做阻塞操作

### P7 — 文档互链与功耗数据更新（0.25 人日，P2）

- **动作**：`README.md` 增加与 `TianshangHealth` 的互链、协议文档入口、双仓库同步说明；若 P6 改变了广播占空比，更新 `docs/POWER_BUDGET.md` 对应数值与推导
- **验收**：Markdown 链接可达；`POWER_BUDGET.md` 数值与 P6 实测/推算一致

### P8 — CI（0.5 人日，P2 / B11）

- **动作**：新增 `.github/workflows/firmware-ci.yml`：
  - job `host-tests`：`ubuntu-latest` + 系统 gcc，编译 `tests/host/*.c` 并运行（无 IDF 依赖，靠 `tests/host/stubs/`）
  - job `build`：`espressif/idf:release-v5.4` 容器，矩阵 `esp32p4` / `esp32s3`，`idf.py set-target && idf.py build`
- **验收**：CI 绿（首次可能需要调整 stub 覆盖）；不引入任何网络依赖到固件本身（CI 下载工具链不属"固件联网"）
- **禁止**：不得为了过 CI 而删除测试或放宽断言

### P9 — 收尾与 pin 冻结（0.25 人日）

- **动作**：`AGENTS.md` §5 文档同步表增行（协议/向量/pin）、新增接线章节状态更新、§8 归档旧计划（`PLAN_TianshangPulse.md` 加"已被取代"横幅）；`protocol-pin.json` 置 `status: FROZEN` 并复制到 Health；`docs/PROTOCOL_VECTORS.md` 复制到 Health
- **验收**：§7.4 校验命令输出全 `True`
- **禁止**：不得在 `.md` 中写入版本号或哈希值

### P10 — LE 最小绑定（0.5 人日，P0 / D3；**第一批，与 Health T5b 同期**）

- **前置**：P1（广播可被发现）
- **实现要点**
  - `ble_gatt_server_init()` 配置 Security Manager：`ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT`、`ble_hs_cfg.sm_bonding = 1`、`ble_hs_cfg.sm_sc = 1`、`ble_hs_cfg.sm_mitm = 0`（Just Works：无 PIN、配对无感，但足以阻止陌生设备）
  - `firmware/sdkconfig.defaults` 增 `CONFIG_BT_NIMBLE_NVS_PERSIST=y`（否则重启后绑定丢失，每次都要重新配对）
  - 处理 `BLE_GAP_EVENT_REPEAT_PAIRING`：**删除旧绑定并允许重新配对**（否则用户换手机/删绑定后设备永久连不上）
  - 处理 `BLE_GAP_EVENT_ENC_CHANGE`：记录加密结果日志（本期仅日志，未加密连接不做强制断开）
- **验收**：双目标 `idf.py build` 通过；实机：首次连接触发系统配对（无感）→ 全特征值可用；**另一台未配对手机连接必须失败**；手机侧删除绑定后可重新配对成功
- **禁止**：不得把绑定失败设计成"设备无法使用"（应允许用户删绑定重来）；不得跳过 `REPEAT_PAIRING`（这是换机场景的唯一出路）

---

## 6. 验证方案

### 6.1 host（无实机可完成）

| 手段 | 覆盖 | 命令 |
| --- | --- | --- |
| 协议契约测试 | 线格式（V1–V10f + 负例） | `scripts/run_host_tests.ps1`（或 `.sh`） |
| 离线缓存测试 | ring / peek / pop / 临界区 | 同上一并运行 |
| parity harness | 训练-推理一致性（不得回归） | 见 `scripts/parity_check.py`（需 numpy） |

**明确不能验证的**：真实广播是否被手机扫到、链路时序、功耗、连接稳定性 → 必须实机。

### 6.2 双目标编译（每张卡的门槛）

```bash
idf.py set-target esp32p4 && idf.py build
idf.py set-target esp32s3 && idf.py build
```

### 6.3 实机（ESP32-P4 / S3）—— **建议先用 ESP32-S3 DevKitC（约 ¥40）**

> **硬件建议（性价比最高的单项投入）**：不要等 P4 手表硬件。固件已支持双目标（`platform/` 兼容层 + `sdkconfig.defaults.esp32s3`），一块 **ESP32-S3 DevKitC** 配 `CONFIG_SENSOR_SIM_PPG=y` **不需要任何传感器**即可验证全链路。完全没有实机时，以下三项无法确认，且恰是 P1/P10 的成败判据：① 广播载荷实际内容（S2/S5）；② 配对是否真的拦住未绑定设备；③ MTU 实际协商值。

| # | 步骤 | 期望 |
| --- | --- | --- |
| 1 | 上电后 nRF Connect 扫描 | 可见设备名 `TianshangPulse`，广播中含 `0xFFF0` |
| 2 | 手机连接 | APP 侧 6/6 特征值校验通过（修复后 7/7 若已加 `0xFFF5`） |
| 3 | 写 `0xFFF5`（APP 侧） | 串口日志 `time sync: epoch=…`；断连重连后时间仍正确 |
| 4 | 制造 AF 事件（`CONFIG_SENSOR_SIM_PPG_AF=y`） | APP 收到真实时间戳（非 1970） |
| 5 | 断连期间累计 ≥25 条事件 → 重连 | APP 分 2 轮拉完，日志 `batch ack: cleared N events` |
| 6 | 写 `0xFFF1` | 日志 `config: hr_thr=… spo2_thr=…` |
| 7 | 空闲电流 | 切到 SLOW 后电流下降（有功耗仪时量化） |
| 8 | 第二台**未配对**手机连接 | **连接失败**（P10 绑定生效） |
| 9 | 手机侧删除绑定 → 重新连接 | 可再次配对成功（`REPEAT_PAIRING` 生效） |

### 6.4 完成定义（DoD）

1. `scripts/run_host_tests.ps1` 全 PASS，输出留档
2. 双目标 `idf.py build` 通过（P4 + S3）
3. 协议变更已按 §7 六步走完，`protocol-pin.json` 与 Health 侧 byte-identical
4. 未引入任何网络能力；未破坏 `offline_cache` / parity 既有测试
5. 新增常量均有 `#define`/`const`，无魔法数（AGENTS §4）

## 7. 双仓库同步措施（**本节规则与 Health 侧逐字一致**；差别仅在于本仓库是**先改方/生成方**）

### 7.1 单一真源

| 内容 | 真源 | 本仓库的角色 |
| --- | --- | --- |
| 协议定义 | **本仓库** `docs/PROTOCOL.md` | 定义方（**先改**） |
| 契约向量（数值） | **本仓库** `docs/PROTOCOL_VECTORS.md` | 生成方（**先改**） |
| 版本号 / 哈希 / 待决项 | `protocol-pin.json` | 生成方；同名文件必须与 Health 侧 **byte-identical** |

**数值唯一性规则**：SHA-256 哈希值**只允许出现在 `protocol-pin.json`**（任何 `.md` 文档都不得复制哈希）。版本号的唯一权威值同样在 pin 中；文档若引述版本号，必须标注"以 `protocol-pin.json` 为准"。
**生成方向不可逆**：哈希只在本仓库重算，然后整体复制到 Health；**禁止**在 Health 侧重算后回填本仓库。

### 7.2 四个同步点（任何协议变更必须全部走完）

1. `protocol-pin.json`（两仓库 byte-identical）
2. `docs/PROTOCOL.md`（两仓库 byte-identical）
3. `docs/PROTOCOL_VECTORS.md`（两仓库 byte-identical）
4. 两仓库 `AGENTS.md` 的接线章节（结构对应，只写规则与路径）

### 7.3 变更流程（六步，顺序不可颠倒；本仓库负责第 1–3 步并执行第 4 步的复制）

| 步 | 仓库 | 动作 | 完成标志 |
| --- | --- | --- | --- |
| 1 | **Pulse** | 修改 `PROTOCOL.md` 并升 `protocol_version`（MINOR = 兼容新增；MAJOR = 改语义/长度；PATCH = 裁决与措辞） | 文档 diff 明确 |
| 2 | **Pulse** | 生成/更新 `PROTOCOL_VECTORS.md`（新增向量，旧向量**不得改值**） | 固件 host 测试可断言新向量 |
| 3 | **Pulse** | 重算两个 SHA-256，更新 `protocol-pin.json`（`status: FROZEN`） | pin 更新 |
| 4 | Pulse → Health | 把三个文件整体复制到 Health（**byte 级，不得编辑**） | 两仓库哈希一致 |
| 5 | 双端 | 各自实现并跑契约测试 | 两端测试全绿 |
| 6 | 双端 | 各自更新计划文档 / AGENTS.md 状态 / `TEST_LOG.md` | 文档同步 |

### 7.4 强制校验命令（提交协议相关改动前必跑）

```powershell
# 在 Project13 根目录执行：① 两仓库 pin 是否一致 ② 镜像文件是否等于 pin 中的哈希
python -c "import json,hashlib,os;b=r'F:\Projects\Project13';rs=['TianshangPulse','TianshangHealth'];p=json.load(open(os.path.join(b,'TianshangPulse','protocol-pin.json'),encoding='utf-8'));print('pin identical:',open(os.path.join(b,rs[0],'protocol-pin.json'),'rb').read()==open(os.path.join(b,rs[1],'protocol-pin.json'),'rb').read());[print(r,t['target'],hashlib.sha256(open(os.path.join(b,r,t['target']),'rb').read()).hexdigest()==t['value']) for r in rs for t in p['lock_files']]"
```

期望输出：`pin identical: True` + 四行 `True`（2 仓库 × 2 文件）。

### 7.5 红线（违反即回滚）

- ❌ 未升协议版本就改线格式（含"临时多一个字节"）
- ❌ 改了线格式而不更新 `PROTOCOL_VECTORS.md` / `protocol-pin.json`
- ❌ 让 Health 侧先改协议（顺序只能是 **Server 先，Client 后**）
- ❌ 在 `.md` 文档中复制哈希值
- ❌ 提交与 pin 哈希不符的 `docs/PROTOCOL*.md`
- ❌ 以"代码就是这样"为由绕过向量

### 7.6 发现不一致时怎么办

以 `docs/PROTOCOL_VECTORS.md` 为**唯一裁决依据**（本仓库是它的生成方，发现问题**必须**先修向量与实现，再重算哈希并同步）；先把矛盾登记到 `protocol-pin.json` → `open_items`，再暂停相关实现并上报。

---

### 7.7 ⚠️ 仓库跟踪对称性（提交前必查）

| 文件 | Pulse 侧 | Health 侧 |
| --- | --- | --- |
| `docs/PROTOCOL.md`、`docs/PROTOCOL_VECTORS.md`、`protocol-pin.json`、`AGENTS.md`、`PLAN*.md` | 已跟踪，`git add` 正常 | **被 `.gitignore` 忽略**（`*.md`/`*.json` 全局规则），见 Health 侧计划文档 §7.7 |

**对本仓库的要求**：提交协议变更时，`git status` 必须能看到 `docs/PROTOCOL_VECTORS.md`、`protocol-pin.json`、`PLAN_WATCH_INTEGRATION.md`（若看不到，说明被误加入忽略规则，需立即排查）。

**跨仓库要求**：复制到 Health 的三个文件不因"是否为 git 跟踪"而放弃同步——**磁盘字节一致 = 契约一致**，校验以 §7.4 的哈希命令为准。

---

## 8. 里程碑与执行顺序

| 里程碑 | 内容 | 完成标志 | 依赖 |
| --- | --- | --- | --- |
| **M1 可被发现且不可被陌生人连接** | P1、P10 | 广播含 `0xFFF0` + 设备名；未配对设备连接**失败**；`idf.py build` 通过 | 实机（无实机则标记未验证） |
| **M2 时间可校准** | P2、P3 | `0xFFF5` 可用、`PROTOCOL.md` 与实现一致、pin 已同步 | 用户批准 D1/D2 |
| **M3 契约可验证** | P4、P8 | host 契约测试 ALL PASS；CI 绿 | — |
| **M4 可联调** | M1 + M2 + M3 | 与 Health 侧 M1/M2 一起跑通 §6.3 | 实机 + 两侧同步 |
| **M5 收尾** | P5–P7、P9 | 文档/CI/pin 全部一致 | M4 |

**推荐执行顺序（四波，关键路径加粗）**：

1. **第 0 波**：**P1**（广播修复；代码就绪即可提交，实机验证待硬件）
2. **第 1 波（连通层）**：**P10**（最小绑定，与 Health T5b 同期）→ **P3**（文档裁决）→ **P4**（协议纯函数 + host 契约测试）→ P8（CI 门禁）
3. **第 2 波（协议升版）**：待用户批准 D1/D2 → **P2**（`0xFFF5` 读写 + epoch 语义）→ **复制三个文件到 Health**
4. **第 3 波（收尾）**：P6 → P5 → P7 → P9

- `P1` 最优先：不修广播，两侧都无法联调，其他一切工作都无法验证
- `P10` 必须与 `P1` 同批：绑定会改变连接流程，晚做要重跑整套扫描/连接/配对验证
- `P4` 必须在 `P2` 之前：`ble_crc8` 要先搬进 `protocol.c`，否则 `P2` 会二次返工
- `P2` 是 Health 侧 T4 的**唯一外部依赖**，批准 D1/D2 后应尽快完成并把三文件复制到 Health
- `P6`/`P5` 可与 Health 侧工作并行（纯固件内部，无协议影响）

| 波次 | 包含任务卡 | Pulse 侧工时 | 目标 |
| --- | --- | --- | --- |
| **第一批（连通层）** | P1、**P10**、P3、P4、P8 | **≈ 2.5 人日** | 可被发现、**不可被陌生人连接**、协议被向量锁定、CI 有门禁 |
| **第二批（协议升版 + 收尾）** | P2、P5、P6、P7、P9 | **≈ 2.5 人日** | 时间可校准、配置持久、功耗达标 |
| **合计** | P1–P10 | **≈ 5.0 人日**（+ Health 侧 ≈9.5 ≈ **14.5**） | — |

> **工时口径（2026-09-20 第二次修订）**：相较首版（MVP 2.5 / 完整 4.5）——新增 **P10 最小绑定**（+0.5）、P8（CI）前移进第一批；**D3 由"不做"上调为"本期做"是本次上调的唯一原因**。

---

## 9. 风险与缓解

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R1 | **无实机 → 广播修复无法验证**（P1 只能靠代码审查与日志） | M1/M4 无法确认 | 交付时明确标注"未在实机验证"；请持有实机的一方按 §6.3 第 1 步抓包确认 |
| R2 | 广播包空间溢出（flags + 2 个 16 位 UUID + 名称 = 24B/31B，余量小） | `ble_gap_adv_set_fields` 返回错误 → 无广播 | 严格核算字节数；名称超长时整体移到 scan response；**必须**检查返回值并打日志 |
| R3 | Health 侧滞后于协议升版（客户端写不存在的 `0xFFF5`） | 客户端写操作失败 | 客户端应把"特征值不存在"当作可降级情况（Health T4 已要求）；Server 侧无需兼容代码 |
| R4 | `P4` 抽取 `protocol.c` 时改变 `gatt_server.c` 行为 | 回归（最坏：0xFFF1 解析错） | host 契约测试 + 双目标编译 + **不得**改 GATT DB 与 UUID |
| R5 | `P8` 首次引入 ESP-IDF 容器 CI，构建耗时长/易失败 | CI 不可用 | 只跑 `build` 不跑烧录；缓存组件与工具链；先在本地验证两个 target 的 CMake 目标名 |
| R6 | 空口明文（B5/D3） | 健康数据在无线空口可被嗅探 | 与 Health 侧同步决策：文档显式声明，或升级为 P0 启用 LE Secure Connections |
| R7 | `P6` 改广播间隔后整机连接变慢 | 用户体验下降 | 保留 30s FAST burst；与 Health 侧"设备记忆直连"（T5）配合减少对扫描的依赖 |
| R8 | 两仓库漂移 | 联调时互相扯皮 | §7 的四个同步点 + 两份同名计划文档 + 校验命令 |

---

## 10. 明确不做（范围排除）

- ❌ 传输层加密 / 配对绑定（D3 暂缓；若升级为 P0 则另开计划）
- ❌ 固件 OTA、`0xFFF6` 体征上传、原始 PPG 波形上传
- ❌ 离线缓存的掉电持久化（B8，列为后续）
- ❌ 修改 GATT DB 既有 6 个特征值的 UUID 与字节布局（只允许**新增** `0xFFF5`）
- ❌ 修改 `offline_cache` / parity 既有语义（已修且有测试）
- ❌ 引入 WiFi/云等任何网络能力（永久排除）
- ❌ 修改 `TianshangHealth` 仓库任何文件（由 Health 侧独立 Agent 负责）

---

## 11. 待用户确认（阻断项）

### 11.1 设计决策（Server 视角）

| ID | 推荐 | 你的决定 |
| --- | --- | --- |
| D1 | 新增 `0xFFF5` 时间同步（**Write + Read 回读**，MINOR 升级） | ☐ 同意 / ☐ 否决 |
| D2 | `type` 最高位作"未校准"标志 | ☐ 同意 / ☐ 否决 |
| D3 | **本期做"最小绑定"**（Just Works + bonding + `REPEAT_PAIRING`）—— 相对首版从"不做"上调 | ☐ 同意 / ☐ 仍不做（则须在 README/SECURITY.md 声明"任何人可连接并读取数据"） |
| P5 | 是否顺带做 NVS 配置持久化（0.5 人日） | ☐ 做 / ☐ 不做 |
| P10 | 是否开启 `CONFIG_BT_NIMBLE_NVS_PERSIST=y`（绑定持久化前提；不开则每次重启都要重新配对） | ☐ 开 / ☐ 不开 |

### 11.2 范围

- ☐ **第一批（连通层）**：P1、P10、P3、P4、P8（≈2.5 人日）
- ☐ **第二批**：P2、P5、P6、P7、P9（≈2.5 人日）
- ☐ **全部**：P1–P10（≈5.0 人日）

### 11.3 实机

- ☐ 有 ESP32-P4 / ESP32-S3 实机 → 可完成 M1/M4 的真实广播与联调验证
- ☐ **建议新增**：采购 **ESP32-S3 DevKitC（约 ¥40）**，配 `CONFIG_SENSOR_SIM_PPG=y` 即可验证全链路（**性价比最高的单项投入**）
- ☐ 无实机 → 本轮只做代码 + 编译 + host 测试，M1/M4 标记"未验证"（广播载荷 / 配对拦截 / MTU 实际值 三项无法确认）

> **答复后的动作**：执行 Agent 必须把结论写回本文件 §11（勾选）+ `protocol-pin.json` → `pending_decisions`（删除已决项），并在提交信息中注明。

---

*初版：2026-09-20 · 对端文档：`TianshangHealth/PLAN_WATCH_INTEGRATION.md`*



