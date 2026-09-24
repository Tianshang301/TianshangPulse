# HANDOFF_REPORT_HEALTH_SIDE — 交接报告（TianshangHealth 侧）

> **生成方**：TianshangHealth 侧 Agent（GATT Client / 协议遵循方）
> **验收依据**：`AGENT_HANDOFF_TIANSHANGHEALTH.md` §4（报告规格）与 `PLAN_WATCH_INTEGRATION.md`（任务卡 T0–T11）
> **真源**：`TianshangPulse` 仓库三件套（`docs/PROTOCOL.md` / `docs/PROTOCOL_VECTORS.md` / `protocol-pin.json`），本仓为只读镜像
> **数值纪律**：本报告**不含**协议版本号与 SHA-256（按 AGENTS.md §9.1，只允许出现在 `protocol-pin.json`）
> **状态图例**：✅ 已完成并验证 ｜ ⚠️ 部分/需修正 ｜ ⏳ 待办/未实机 ｜ 🚫 禁止
>
> **修订说明（本轮对齐，2026-09-24）**：原稿 §4 / §5 / §6 / §9.2 共 9 处称 D1–D6「已批准 / 已确认」。经核查为**误归属**——两仓 `PLAN_WATCH_INTEGRATION.md` 状态仍为「待用户拍板」、§11.1 复选框全空，`protocol-pin.json` 的 `pending_decisions` 仍含 D1–D6。已统一改为「按 PLAN §4 默认规则先行实现、**未获批准**」；详见 §4 末行。另修正 §3 覆盖计数歧义、§8 的 git 快照失效问题。

---

## 1. 必读文件清单（R1–R9 + H1–H7，覆盖率 100%）

### 1.1 TianshangPulse 仓库（只读）

| 序 | 文件 | 已读 | 一行核心结论 |
| --- | --- | --- | --- |
| R1 | `AGENTS.md` | ✅ | 协议变更六步 / 数值唯一性 / 四个同步点 / 代码红线 / §3.3 问题上报格式；生成方向不可逆（哈希只在 Pulse 侧重算） |
| R2 | `protocol-pin.json` | ✅ | 版本与两文件 SHA-256 唯一真源；`wire_constraints` 明确 0xFFF1/3/4 写类型、0xFFF4 无整批 CRC；O-1..O-6 开放项；D1–D6 待决（scope=分批） |
| R3 | `docs/PROTOCOL.md` | ✅ | §6 仍写「末尾带 CRC-8」，与向量矛盾（O-1）；§5 `0xFFF3` 只写 1 字节与向量 2 字节冲突（D-A）；其余特征值布局与实现一致 |
| R4 | `docs/PROTOCOL_VECTORS.md` | ✅ | 状态 FROZEN，24 条黄金向量 V1–V10f + 反例 P6a/P6b/P7/V9/V9b/V10c/V10f；**唯一裁决依据** |
| R5 | `HANDOFF_TIANSHANGHEALTH.md` | ✅ | 接口说明书：§3.2 扫描 P0、§3.3 MTU/写类型、§3.5 权限、§4 UUID 展开、§5 CRC-8、§6 六接口、§7 时间戳降级、§8 O-1..O-6+D-A..D-D、§10 契约测试、§11 任务清单 |
| R6 | `PLAN_WATCH_INTEGRATION.md` | ✅ | Server 侧任务卡 P1–P10 + 决策 D1–D6 + 四波执行顺序；P2 含 `0xFFF5` 需 Client T4 配合 |
| R7 | `firmware/main/ble/gatt_server.c` | ✅ | `gatt_db[]` 6 特征值；`ble_crc8`（poly 0x07/init 0x00）；`ble_pack_anomaly` 11B；`0xFFF4` peek/ACK-pop；**无 adv fields**（B1）；**无 sm_* 配置**（B17/D3）；`ble_notify_spo2` 0 处调用（D-C） |
| R8 | `tests/host/test_offline_cache.c` + `stubs/` | ✅ | host 单测组织：桩头文件 + `main()` 断言；协议契约测试（`test_protocol.c`）列为 P4 新增 |
| R9 | `docs/SYNC_HARDWARE_PREP.md` | ✅ | 16MB Flash + ILI9341 驱动完工；S3/P4 双目标编译通过；**全部链路行为未在实机验证** |

### 1.2 TianshangHealth 仓库（主场）

| 序 | 文件 | 已读 | 一行核心结论 |
| --- | --- | --- | --- |
| H1 | `AGENTS.md` | ✅ | 接线章节与 R1 §9 同源；§3.2 列名红线、§10 第 6 条（不确定选更严格）、§21.3 实现约束 |
| H2 | `PLAN_WATCH_INTEGRATION.md` | ✅ | 任务卡 T0–T11 + 断点 B1–B17 + 四波执行顺序 + 验证方案（§6.1 无实机路径） |
| H3 | `README.md` | ✅ | 模块清单原缺 `feature/watch`；**本轮已更新**（新增 Watch 特性、BLE 权限表、显式声明不需要 `INTERNET`） |
| H4 | `.gitignore` | ✅ | `*.md` / `*.json` 全局忽略（O-6）；**本轮已放行** `!/docs/PROTOCOL*.md`、`!/protocol-pin.json`、`!/HANDOFF_REPORT_HEALTH_SIDE.md` |
| H5 | `feature/watch/.../ble/BleManager.kt` | ✅ | 原 `startScan` 用 `ScanFilter` 过滤（O-4/B1）；**本轮已放宽**为无过滤 + 连接后服务发现兜底 |
| H6 | `feature/watch/.../ble/ProtocolParser.kt` | ✅ | `crc8()` 存在且与 R4 §1 逐位一致；`parseAnomalyEvent` / `packUserConfig` / `packModelSwitch` / `splitOfflineBatch` 与向量一致 |
| H7 | `core/database/`（schema + migrations） | ✅ | `anomaly_events` **无 `userId`**（O-5）；`DatabaseModule` 原含 `fallbackToDestructiveMigration()`（D6）**本轮已移除**；`vitals_samples` **不存在**（D4 待第二批） |

---

## 2. 仓库现状核查

### 2.1 模块列表

| 模块 | 是否存在 |
| --- | --- |
| `feature/watch` | ✅ **存在**（`settings.gradle` 已注册，`app/build.gradle` 已依赖，导航 + Dashboard 入口已接入） |
| `core/pulse-protocol` | ❌ 不存在（按 D5 第二批抽取，本轮不建） |

### 2.2 `BleManager.kt` 过滤代码（O-4 的处置现场）

- **原实现**（已修复）：`startScan(filters, settings, callback)` 构造 `ScanFilter.Builder().setServiceUuid(0xFFF0)` 做硬件级过滤。
- **本轮改后**（`BleManager.kt:130-138`）：`startScan(emptyList(), settings, callback)` —— **不设任何硬件过滤**；`onScanResult` 经 `WatchDeviceMatcher.matches(advName, deviceName, serviceUuids)` 做名称前缀判定，结果再以 `hasRequiredCharacteristics()`（连接后服务发现）作最终身份校验（C3 兜底）。

### 2.3 `ProtocolParser.crc8()`

- 存在：`ProtocolParser.kt:40-53`。
- 参数：poly `0x07` / init `0x00` / **无反射** / **无最终异或**，MSB-first，与 `gatt_server.c::ble_crc8()` 逐位一致，并被 `ProtocolCodecTest` V1/V1b/V1c 断言锁定。

### 2.4 `.gitignore` 实际规则（O-6 处置后）

```
*.md                    ← 全局忽略
!README.md
!readme/*.md
!/docs/PROTOCOL.md       ← 放行（契约镜像可审计）
!/docs/PROTOCOL_VECTORS.md
!/HANDOFF_REPORT_HEALTH_SIDE.md
*.json
!/protocol-pin.json
```

> 已验证：`git check-ignore -v protocol-pin.json` / `docs/PROTOCOL*.md` / 报告文件均显示被白名单放行；`AGENTS.md` / `PLAN*.md` 仍被忽略（方案 A 仅放行契约文件，符合 O-6「仓库所有者决定」项）。

### 2.5 Room 迁移链与 `anomaly_events`

- `TianshangDatabase.kt`：version = **2**，9 张表，`exportSchema = true`。
- `Migrations.kt`：`MIGRATION_1_2`（非破坏建 `anomaly_events` + 2 索引）。
- `DatabaseModule.kt`：`addMigrations(MIGRATION_1_2)`，**已无** `fallbackToDestructiveMigration()`（D6 完成）。
- `AnomalyEvent` 实体字段：`id/typeCode/timestampMs/confidence/valid/createdAt` —— **无 `userId`**（O-5，第三批 `MIGRATION_3_4` 评估）。
- `AnomalyEventDao`：新增 `exists(typeCode, timestampMs)` 供查询式去重（B13）。

---

## 3. 协议一致性自查（对照 R4 的 24 条向量逐条）

> 断言载体：`feature/watch/src/test/.../ble/ProtocolCodecTest.kt`（30 个测试方法）。同一 ID 的期望字节仅以 `docs/PROTOCOL_VECTORS.md` 为准，测试内以显式 hex 常量落地并标注向量 ID。

| ID | 内容 | 状态 | 代码位置 / 说明 |
| --- | --- | --- | --- |
| V1 | CRC-8 `ff→f3` | ✅ 一致 | `ProtocolParser.crc8`；`ProtocolCodecTest` `V1 crc8 of ff is f3` |
| V1b | CRC-8 `01→07` | ✅ 一致 | 同上，`V1b` |
| V1c | CRC-8 `12 34→f1` | ✅ 一致 | 同上，`V1c` |
| V2 | 全零边界，CRC=`00` 合法 | ✅ 一致 | `parseAnomalyEvent`；`V2 all-zero boundary`（`valid=true`） |
| V3 | SpO2 异常（type=1, ts=1700000000000, conf=92） | ✅ 一致 | `V3`（typeCode=1, ts, conf 逐字段断言） |
| V4 | AF 异常（type=3） | ✅ 一致 | `V4` |
| V5 | 模型切换事件（ts 极小值 4095） | ✅ 一致 | `V5` |
| V5b | 边界 confidence=0 | ✅ 一致 | `V5b` |
| V6 | 用户配置 hr=100/spo2=92/g0/age25 → 8B | ✅ 一致 | `packUserConfig` 恒 8 字节；`V6` 逐字比对 |
| V6b | hr=200/spo2=90/g1/age30 | ✅ 一致 | `V6b` |
| V6c | 全零边界 | ✅ 一致 | `V6c` |
| V6d | 字段极值（65535/100/1/65535） | ✅ 一致 | `V6d` |
| V7 | 模型切换 idx=1 → `01 07` | ✅ 一致 | `packModelSwitch` 恒 2 字节；`V7` |
| V7b | idx=0 → `00 00` | ✅ 一致 | `V7b` |
| V8a | 离线批量 3 条（33B，ev0/ev2 逐字） | ✅ 一致 | `splitOfflineBatch`；`V8a` |
| V8b | 满批 20 条（220B） | ✅ 一致 | `V8b` |
| V9 | 坏 CRC 必须 `valid=false` | ✅ 一致 | `V9 bad CRC` |
| V9b | 短包返回 `null` | ✅ 一致 | `V9b short packet`（长度不足不抛异常） |
| V10 | HR `01 00→1` | ✅ 一致 | `parseHeartRate`；`V10` |
| V10b | HR `ff ff→65535` | ✅ 一致 | `V10b` |
| V10c | HR 单字节→`null` | ✅ 一致 | `V10c`（返回 null，不抛异常） |
| V10d | SpO2 `64→100` | ✅ 一致 | `parseSpO2`；`V10d` |
| V10e | SpO2 `00→0` | ✅ 一致 | `V10e` |
| V10f | SpO2 空数组→`null` | ✅ 一致 | `V10f` |
| P6a | Client 不得构造 7 字节配置 | ✅ 已防御 | `packUserConfig` 恒 8 字节断言 |
| P6b | CRC 错不得被 Client 构造 | ✅ 已防御 | `packUserConfig` CRC 恒正确断言 |
| P7 | `idx > 1` Client 拒绝 | ✅ 已防御 | `packModelSwitch(2) → null` |

**结论**：24 条向量 + 3 条反例全部 ✅ 一致，无 ⚠️/❌。

> **计数口径消歧（本轮补充）**：本表 27 行 = 24 条向量 + 3 条反例（`V8`/`V6`/`V7` 等是向量分组标题，非独立向量，故 R4 的向量 ID 共 24 个）。而 `ProtocolCodecTest.kt` 共 **30 个 `@Test` 方法**，其中 27 个方法名带向量/反例 ID，另 3 个（空批、长度非 11 倍数、超大批）是无向量 ID 的额外边界测试。**「27」与「30」不是同一指标**——前者是向量覆盖数，后者是测试方法数。`AGENTS.md §21.4` 与本报告的「27 / 30」两处数字分别取自这两条口径，并非矛盾，但应避免混用。

---

## 4. 矛盾与待上报项（逐条：现象 / 证据 / 影响 / 建议归属）

> **明确声明：本轮未修改任何协议真源文件**（`docs/PROTOCOL.md` / `docs/PROTOCOL_VECTORS.md` / `protocol-pin.json`），两仓字节一致已复核（§8）。

| # | 现象 | 证据 | 影响 | 建议归属 |
| --- | --- | --- | --- | --- |
| O-1 | `PROTOCOL.md` §6 写「末尾带 CRC-8」，与向量「不追加整批 CRC」矛盾；且整批 `crc8(msg)` 恒 `00` | R3 ↔ R4 §6 / O-1 | Client 若照文档实现会恒误判 | **Pulse** 走六步流程修订 §6（`N × 11` + 逐条 CRC）并升版 |
| O-2 | `timestamp_ms` 固件发开机以来 ms，非 epoch ms | `main.c:190` ↔ R4 §9 | 直接展示会出现 1970 | **双端**；D1 按推荐做 `0xFFF5`（**待用户拍板**），T4 待 Pulse 落地后实现；未实现前 Client 按降级展示 |
| O-3 | `0x2A37`/`0x2A5F` 偏离 BLE 标准结构 | R4 §8 / O-3 | 第三方客户端无法解析 | **Pulse**（记录即可，Client 已按私有格式解析并注释） |
| O-4 | 广播无 `0xFFF0` UUID、不保证设备名 | `gatt_server.c:133-146` ↔ B1 | 发现失败 | **双端**；Client 侧 B1 已修复（§2.2）；Pulse 补 adv fields 后自动生效 |
| O-5 | `anomaly_events` 缺 `userId` | H7 / R5 §8.1 | 事件无法按用户隔离 | **Health** 第三批 `MIGRATION_3_4`（不阻塞本轮） |
| O-6 | `.gitignore` 忽略 `*.md`/`*.json` | H4 | 治理文档不入 git、漂移不可检测 | **Health** 本轮已按方案 A 放行契约文件 |
| D-A | `0xFFF3`：文档 §5 只写 1 字节；固件不校验 CRC、不强制 2B；向量要求 2B | R3 §5 ↔ R4 §5 ↔ `gatt_server.c:251-263` | Client 按向量发 2B 即兼容；固件校验过松 | **Pulse**（✅ 已登记 `open_item`；⏳ 文档 §5 收紧 + 固件补 CRC 校验仍待解决） |
| D-B | `0xFFF1`：文档 §3 变长保留；固件接受 8–32B；向量恒 8B | R3 §3 ↔ R4 §4 ↔ `gatt_server.c:226-248` | Client 恒发 8B；固件过松 | **Pulse**（文档收紧） |
| D-C | `0x2A5F` 血氧「有定义、无发送」 | R7：`ble_notify_spo2` 0 处调用 | Client 收不到血氧；验收不得以血氧为通过条件 | **Pulse**（固件接线） |
| D-D | `0xFFF1`/`0xFFF3` 写入暂无下游消费 | R7：`ble_get_user_config`/`ble_get_model_index` 无调用 | 写入成功但手表行为不变；验收以日志为准 | **Pulse**（后续接线） |
| B5/O-2 现状 | 当前代码 `WatchScreen.formatTimestamp` 直接用 `Date(timestampMs)` 展示 | `WatchScreen.kt:280-283` | 未同步前可能显示 1970 | **双端**；T4 实现 `0xFFF5` + 未校准标志 + 量级兜底前，属已知待决 |
| 新增观察 | `values-en` 及多语言缺 `sleep_*` 等 16–24 条键（含 `error_failed_delete`、`toggle_exact_number`、`wallpaper_crop_title`） | `check_strings.py` 实测 | 睡眠增强功能的多语言缺口 | 属 `8710d19`（sleep 特性）遗留，**非本轮 watch 范围**，已登记不阻塞 |
| **自曝 · 授权误归属**（本轮已修正） | 原稿 §5 / §6 / §9.2 共 9 处称 D1–D6「已批准 / 已确认」 | 两仓 `PLAN_WATCH_INTEGRATION.md` 仍标「待用户拍板」、§11.1 复选框全空；`protocol-pin.json` 的 `pending_decisions` 仍含 D1–D6 | **虚假授权**——可能被下游当成用户已同意而推进协议升版；违反简报红线 #7「不得替用户拍板」 | **Health 侧文档自身**，已统一改为「按推荐先行实现、未获批准」（§5 表头注记） |

---

## 5. D1–D6 影响评估（建议，不代拍板）

> **⚠️ 授权状态更正（本轮）**：本节 6 项**均未获用户批准**。原稿此处标注「已批准」属误归属——两仓 `PLAN_WATCH_INTEGRATION.md` 仍为「待用户拍板」、§11.1 复选框全空，`protocol-pin.json` 的 `pending_decisions` 仍含 D1–D6。已实施的项是**按 PLAN §4「未拍板前一律按推荐列执行」的默认规则先行落地**，其性质是待追认的实现，不是授权。

| ID | 对 Client 的影响 | 建议（标注） |
| --- | --- | --- |
| D1（0xFFF5 时间同步，Write+Read 回读） | 决定时间戳降级是临时还是永久；T4 依赖 Pulse 先落地升版三件套 | **建议，不代拍板**：做 + 回读校验（`clockOffsetMs = (t1+t2)/2 - watch_now`）；**待用户拍板** |
| D2（`type` 最高位 = 未校准） | T4 解析 `type & 0x7F`、置位显示「未校准」；不写 1970 | **建议，不代拍板**：做（MINOR 兼容）；**待用户拍板** |
| D3（最小绑定 Just Works + bonding + REPEAT_PAIRING） | T5b 已实现 `createBond()` + `ACTION_BOND_STATE_CHANGED` + BOND_NONE 降级只读；Pulse 需配 `sm_*` + `NVS_PERSIST` | **建议，不代拍板**：本期做（**待用户拍板**）；Pulse P10 必须同期 |
| D4（仅 `vitals_samples` + HR/SPO2，不写 `DailyHealth`） | T9 建表 + `MIGRATION_2_3` + feature:analysis 直查；排除体温 | **建议，不代拍板**：做（收敛范围）；**待用户拍板**，第二批执行 |
| D5（契约测试分档） | 第一批内联 `feature:watch` 测试（本轮已落地）；第二批抽 `core:pulse-protocol` | **建议，不代拍板**：分档；**待用户拍板** |
| D6（移除 destructive fallback） | 已移除，显式 `MIGRATION_1_2` | 已按推荐执行（第 0 波，独立提交）；**仍待用户拍板** |

---

## 6. 任务执行状态（逐卡）

| 卡 | 内容 | 状态 | 改动文件 | 验收 |
| --- | --- | --- | --- | --- |
| **D6** | 移除 `fallbackToDestructiveMigration` | ✅ | `core/database/.../di/DatabaseModule.kt` | 无该调用；`MIGRATION_1_2` 注册；全量测试绿 |
| **T0** | 前置核查与决策落定 | ⚠️ 部分 | —（评估回填见 §5；基线命令输出见 §7） | 前置核查与 `test detektAll` 绿；**决策未落定**——D1–D6 未获批准，已按 PLAN §4 默认规则先行执行 |
| **T1** | 发现链路修复（B1） | ✅ | `BleManager.kt`（扫描放宽）、`WatchDeviceMatcher.kt`、`BleConstants.kt` | `WatchDeviceMatcherTest` 6 用例绿 |
| **T2** | GATT 操作串行化（B2） | ✅ | `GattOperationQueue.kt`、`GattOperation.kt`、`BleManager.kt` | `GattOperationQueueTest` 6 用例绿 |
| **T3** | 订阅可观测化 + MTU 结果（B3） | ✅ | `BleManager.kt`（`onMtuChanged`、`subscriptions` StateFlow、status 处理） | 单测覆盖；编译绿 |
| **T5b** | LE 最小绑定（D3） | ✅ | `WatchBondController.kt`、`WatchBondTracker.kt`、`WatchBleService.kt`、`WatchViewModel.kt`、`WatchScreen.kt` | `WatchBondTrackerTest` 9 用例绿；未配对降级只读 |
| **T6（内联）** | 契约测试第一批内联 | ✅ | `feature/watch/src/test/.../ble/ProtocolCodecTest.kt` | 30 用例（24 向量 + 3 反例 + 边界）全绿 |
| **T7** | 离线批量拉取 + ACK + 去重（B4/B13） | ✅ | `OfflineSyncProcessor.kt`、`WatchBleService.kt`、`WatchRepository.kt`、`AnomalyEventDao.kt` | `OfflineSyncProcessorTest` 7 用例 + `WatchRepositoryTest` 12 用例绿 |
| **T11** | 文档与 CI 收尾（部分） | ⏳ 部分 | `.gitignore`（放行）、`README.md`（BLE 权限 + 无 INTERNET）、`AGENTS.md` §21、`TEST_LOG.md`、schema 清理（B14） | 本报告即 T11 的 Health 侧交付物之一 |

> **第二批未执行**（范围取自 D1–D6 推荐列，**未经用户批准**）：T4（0xFFF5，需 Pulse 先落地升版三件套）、T5a（设备记忆）、T8（下行配置）、T9（DB v2→v3 + `vitals_samples`）、T10（UI 降级态）、T6 抽 `core:pulse-protocol`。

---

## 7. 测试证据（命令 + 结果原文）

### 7.1 契约测试

- 载体：`ProtocolCodecTest.kt`（30 用例）
- 命令：`./gradlew :feature:watch:test`
- 结果：**BUILD SUCCESSFUL**；`ProtocolCodecTest: tests=30 failures=0`

### 7.2 单元测试（feature:watch）

| 测试类 | tests | failures |
| --- | --- | --- |
| `GattOperationQueueTest` | 6 | 0 |
| `OfflineSyncProcessorTest` | 7 | 0 |
| `ProtocolCodecTest` | 30 | 0 |
| `ProtocolParserTest` | 14 | 0 |
| `WatchBondTrackerTest` | 9 | 0 |
| `WatchDeviceMatcherTest` | 6 | 0 |
| `WatchRepositoryTest` | 12 | 0 |
| `WatchViewModelTest` | 9 | 0 |

### 7.3 全量回归 + 静态分析

```
./gradlew test detektAll → BUILD SUCCESSFUL in 1m 32s
   697 actionable tasks: 163 executed, 534 up-to-date
```

全仓 JVM 测试汇总：**92 个测试文件，846 用例，0 失败**。

### 7.4 多语言校验

```
python check_strings.py → Base English keys: 869
   本轮新增 watch_read_only_warning 21/21 语言对齐（0 缺失 watch 键）
   剩余 MISSING：sleep_* 等为 8710d19 遗留（§4 新增观察），非本轮范围
python check_string_resources.py → 2 条 unit_net_kcal_format 已知误报（AGENTS.md §16）
```

### 7.5 双仓库契约校验（§7.4 命令）

```
pin identical: True
TianshangPulse docs/PROTOCOL.md True
TianshangPulse docs/PROTOCOL_VECTORS.md True
TianshangHealth docs/PROTOCOL.md True
TianshangHealth docs/PROTOCOL_VECTORS.md True
```

---

## 8. 边界声明

1. **未在实机验证**：BLE 实际扫描 / 连接 / MTU 协商 / CCCD 写入 / 离线拉取 / 配对拦截的**链路行为全部未验证**（硬件在途）。无实机时三项无法确认：广播载荷实际内容、配对是否拦住未绑定设备、MTU 实际协商值。
2. **本轮改动文件清单**（`git status` 中途快照——**现已不可复现**，见本节末注记）：

   | 类别 | 文件 |
   | --- | --- |
   | 修改 | `.gitignore`、`README.md`、21 × `strings.xml` + `values/strings.xml`、`AnomalyEventDao.kt`、`DatabaseModule.kt`、`BleManager.kt`、`ProtocolParser.kt`、`WatchRepository.kt`、`WatchBleService.kt`、`WatchScreen.kt`、`WatchViewModel.kt`、`WatchRepositoryTest.kt`、`WatchViewModelTest.kt` |
   | 新增 | `docs/PROTOCOL.md`、`docs/PROTOCOL_VECTORS.md`、`protocol-pin.json`、`GattOperation.kt`、`GattOperationQueue.kt`、`OfflineSyncProcessor.kt`、`WatchDeviceMatcher.kt`、`WatchBondController.kt`、`WatchBondTracker.kt`、`GattOperationQueueTest.kt`、`OfflineSyncProcessorTest.kt`、`ProtocolCodecTest.kt`、`WatchBondTrackerTest.kt`、`WatchDeviceMatcherTest.kt`、本报告 |
   | 删除（本地） | `core/database/schemas/**/3.json`–`12.json`（旧 lineage，B14；schemas 目录本身被 `*.json` 忽略，未入 git） |
   | **未改动** | **三件套真源内容未编辑**（仅将 Pulse 侧已存在的镜像纳入 git 跟踪）；**TianshangPulse 仓库任何文件未触碰**；`docs/PROTOCOL.md`、`docs/PROTOCOL_VECTORS.md`、`protocol-pin.json` 未逐字改动（hash 复核一致，见 §7.5） |

   > **注记（本轮补充，VCS 可见性的真实边界）**：上表是**提交前的中途快照**，现已不可再用 `git status` 复现——第一批交付连同本报告已合并在本仓一次提交内，`git status` 现返回空。核验请改用 `git show --stat <commit>`，本报告的入库记录为 `git log --oneline -- HANDOFF_REPORT_HEALTH_SIDE.md`（门槛 G5 在**本仓内**已满足）。但必须同时说明三点边界：
   > - 外层工作区仓 `Project13` **仅跟踪 `.gitignore`**，三处均无 `.gitmodules`，故 `TianshangHealth/` 与 `TianshangPulse/` 在那里整体未跟踪——跨仓 `git log` 查不到本报告。
   > - 本报告在 **Pulse 仓是未跟踪状态**（`??`）；同属 `??` 的还有 `protocol-pin.json`、`docs/PROTOCOL_VECTORS.md`、`PLAN_WATCH_INTEGRATION.md`、`PLAN_TianshangPulse.md`。
   > - 本仓 `.gitignore` 使 `AGENTS.md`、`PLAN*.md`、`TEST_LOG.md`、`AGENT_HANDOFF_TIANSHANGHEALTH.md`、`HANDOFF_TIANSHANGHEALTH.md` 均不可见。
   >
   > **结论**：git 只能覆盖本仓一小部分文件。§7.5 的哈希比对命令**不依赖 git**，才是双仓漂移检测的可靠手段；交接文档的「双仓 byte-identical」只能靠哈希/内容比对校验，不能靠 `git status`。
   >
   > **R2 补充（2026-09-24）**：上表「未改动」行描述的是**本报告（Health 侧）交付时**本仓未逐字编辑三件套——这一事实对 Health 侧依然成立（本报告从未编辑 `protocol-pin.json`）。但 R2 对齐轮（用户授权「登记进 pin」）已向 `protocol-pin.json.open_items` 追加 D-A…D-D 四条元数据（属所有者动作，非本报告动作），**仅元数据**，未动 `lock_files` / `protocol_version` / `pinned_at` / `status`，故 §7.5 的哈希复核（`lock_files` 与 `docs/PROTOCOL.md` / `docs/PROTOCOL_VECTORS.md` 比对）**仍一致**。即：`protocol-pin.json` 文件本身已变（`open_items` 增长），但其锁定的两个哈希未变。

3. 三件套替换留痕：本轮**未发生** Pulse 侧发起的整文件替换（文件在提交前已与 Pulse byte-identical，§7.5 哈希核对结论：**一致**）。
4. `git status` 中 `AGENTS.md`/`PLAN*.md`/`TEST_LOG.md` 仍被忽略属预期（方案 A 仅放行契约文件），其变更记录于本报告与 TEST_LOG。本报告已补充该清单的实际范围，见本节第 2 条注记。

---

## 9. 下一步与阻塞

### 9.1 需要 Pulse 侧做什么（按 R1 §3.3 上报格式）

| # | 模块 | 现象 | 复现 | 预期 | 实际 | 环境 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `firmware/main/ble/gatt_server.c` | 广播载荷未配置 `0xFFF0`/设备名（O-4/B1） | nRF Connect 扫描 | 可见 `TianshangPulse` + `0xFFF0` | 无 adv fields | 实机待验 |
| 2 | `firmware/main/ble/gatt_server.c` | 未配置 Security Manager（B17/D3） | 任意 BLE 客户端连接 | 未配对设备连接失败 | 可直接连上 | 实机待验 |
| 3 | `firmware/main/main.c` | `timestamp_ms` 为开机 ms（O-2/B5） | 连接后看事件时间戳 | epoch ms | 1970 | 实机待验 |
| 4 | `firmware/main/ble/gatt_server.c` | `ble_notify_spo2` 无调用（D-C） | 订阅 `0x2A5F` | 收到血氧 | 无数据 | 实机待验 |
| 5 | `docs/PROTOCOL.md` | §6「末尾带 CRC-8」与向量矛盾（O-1）、§5 `0xFFF3` 1 字节与向量 2 字节冲突（D-A） | 读文档 vs 读向量 | 一致 | 矛盾 | — |
| 6 | `docs/PROTOCOL.md` | §3 `0xFFF1` 保留段变长（D-B） | 读文档 vs 向量 | 恒 8 字节 | 文档写变长 | — |
| 7 | 协议升版 | `0xFFF5` 时间同步（D1）+ 未校准标志（D2） | 走六步流程升 MINOR，落地三件套并复制到本仓 | 版本升位 + 新向量 | 当前 `protocol-pin.json` 为 FROZEN 基线版（数值以该文件为准） | — |

### 9.2 需要用户拍板什么

| # | 项 | 说明 |
| --- | --- | --- |
| 1 | D1/D2/D3/D4/D5/D6 | **仍待用户拍板**（原稿误写为「已批准」，已更正，见 §5 表头注记）。请在 `PLAN_WATCH_INTEGRATION.md` §11.1 逐项勾选；拍板后由 Pulse 侧走六步流程，删除 `protocol-pin.json` → `pending_decisions` 中已决项并重新固定 |
| 2 | 实机采购 | 建议 ESP32-S3 DevKitC（约 ¥40）+ `CONFIG_SENSOR_SIM_PPG=y` 验证全链路；无实机则链路行为保持「未验证」 |
| 3 | 第二批启动时点 | 待 Pulse 侧 P2（`0xFFF5`）+ 三件套复制到本仓后，即可启动 T4/T5a/T8/T9/T10 |
| 4 | 治理文档是否放行入 git | 方案 A 当前仅放行契约文件；`AGENTS.md`、`PLAN*.md`、`TEST_LOG.md`、`AGENT_HANDOFF_TIANSHANGHEALTH.md`、`HANDOFF_TIANSHANGHEALTH.md` 均被忽略。如需治理文档入库请确认（含安全发现内容公开性评估）。注：交接文档入库与否不影响其作为交付物的成立——它们的存在与一致性由内容比对校验，不依赖 git |

---

*由 TianshangHealth 侧 Agent 生成 · 依据 `AGENT_HANDOFF_TIANSHANGHEALTH.md` §4 规格 · 真源以 `protocol-pin.json` 为准*
*修订：2026-09-24 与两份姊妹交接文档对齐——更正 D1–D6 授权误归属（§5）、消歧测试计数口径（§3）、更正 git 可见性陈述（§8）*
*状态：⏳ 第二批（T4/T9/T10）待 Pulse 侧 `0xFFF5` 升版落地；**D1–D6 仍待用户拍板** · 全项目链路行为未在实机验证（硬件在途）*
