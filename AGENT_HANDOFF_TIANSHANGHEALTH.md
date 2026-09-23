# AGENT_HANDOFF_TIANSHANGHEALTH — 对接 Agent 任务简报

> **用途**：TianshangPulse 侧 Agent → **TianshangHealth 侧 Agent** 的交接简报。本文可直接作为提示词投递给对端 Agent。
> **你的必交付物**：读完 §1 规定的全部文件后，按 **§4 规格**撰写《交接报告》并回传用户。
> **与 `HANDOFF_TIANSHANGHEALTH.md` 的分工**：后者是**接口与协议说明书**（必读、细读）；本文是**任务入口 + 必读清单 + 报告规格**。
> **数值纪律**：本文与你产出的报告**均不得**复制协议版本号与 SHA-256（只引用 `protocol-pin.json` 的字段名），依据 AGENTS.md §9.1。
> **状态图例**：✅ 已完成并验证 ｜ ⏳ 待办/未实机 ｜ 🚫 禁止

---

## 0. 角色与仓库边界

| 项 | 内容 |
| --- | --- |
| 你的角色 | **TianshangHealth 侧 Agent**，GATT **Client**（协议**遵循方**，后改） |
| 你的仓库 | TianshangHealth（Android / Kotlin / Gradle / Room / BLE） |
| 对端仓库 | **TianshangPulse**，GATT **Server**（协议**定义方**，先改），本地根路径 `F:\Projects\Project13\TianshangPulse` |
| 对端文档 | `PLAN_WATCH_INTEGRATION.md`（**两仓同名**）、`HANDOFF_TIANSHANGHEALTH.md`、本文 |
| 单向契约 | Server 定义、Client 遵循；发现协议问题 → **上报**，不得自行改协议 |
| 永久约束 | 项目**完全离线**：禁止引入 WiFi / 云 / 任何网络能力；BLE 是唯一对外接口 |

> ⚠️ 若你**无法访问** TianshangPulse 仓库：请先向用户索取 §1.1 的 R1–R9 文件内容，**读到之后再开工**；
> **禁止凭记忆、凭常识补全协议细节**（协议以文件为准，记忆一律视为不可信）。

---

## 1. 必读文件清单（按顺序；每项写明「读它为了什么」）

### 1.1 TianshangPulse 仓库（**只读**，共 9 项）

| 序 | 文件 | 读它为了什么（报告中须逐项给一行结论） |
| --- | --- | --- |
| R1 | `AGENTS.md`（重点 **§9 跨仓协作**、**§4 代码红线**、§3.3 问题上报格式） | 协议变更六步、数值唯一性、四个同步点、代码红线、上报模板 |
| R2 | `protocol-pin.json` | **数值真源**：`protocol_version` / `next_protocol_version`、两个 `lock_files[].value`、`wire_constraints`、`open_items`（O-1…O-6）、`pending_decisions`（D1–D6）、`sync_rules` |
| R3 | `docs/PROTOCOL.md` | 线格式定义（字段布局 / UUID / 属性 / 语义）。**已知其 §6 与向量矛盾**（见 O-1），读时须带着这个前提 |
| R4 | `docs/PROTOCOL_VECTORS.md` | **唯一裁决依据**：24 条黄金向量（V1、V1b、V1c、V2…V10f）+ 反例（P6a/P6b/P7/V9/V9b）；状态 **FROZEN** |
| R5 | `HANDOFF_TIANSHANGHEALTH.md` | **接口说明书（重点读）**：§3.2 扫描 P0、§3.3 MTU/写类型、§3.5 Android 权限、§4 UUID（含 128 位展开）、§5 CRC-8、§6 六个接口逐条、§6.7 GATT 错误处理、§7 时间戳降级、§8 开放项、§10 契约测试、§11 任务清单、§13 边界声明 |
| R6 | `PLAN_WATCH_INTEGRATION.md` | 任务卡与波次、§1 现状事实、§11 **待用户拍板的决策项**、§9 风险 |
| R7 | `firmware/main/ble/gatt_server.c` | Server 实现对照（只读）：`gatt_db[]`、`ble_crc8()`、`ble_pack_anomaly()`、三个 write/read handler、`start_advertising()` |
| R8 | `tests/host/test_offline_cache.c` + `tests/host/stubs/` | host 测试的组织方式与桩文件写法（可作你方 JVM 测试布局参照） |
| R9 | `docs/SYNC_HARDWARE_PREP.md`（选读） | 硬件到货前软件状态：哪些已编译验证、哪些**未在实机验证** |

### 1.2 TianshangHealth 仓库（你的主场，共 7 项）

| 序 | 文件 | 读它为了什么 |
| --- | --- | --- |
| H1 | `AGENTS.md` | 你方 Agent 行为规范 + 接线章节（应与 R1 §9 同源；若冲突**以 R1 侧为协议真源**并上报） |
| H2 | `PLAN_WATCH_INTEGRATION.md` | 对端同名计划：你方任务卡 **T0–T11** 的定义与验收标准 |
| H3 | `README.md` | 模块架构现状：核实是否**真无** `feature/watch` 与 `core/pulse-protocol` |
| H4 | `.gitignore` | 核实 `O-6`：是否忽略全部 `*.md` / `*.json`（直接影响治理文档与你的报告能否入库） |
| H5 | `feature/.../ble/BleManager.kt` | 核实 `O-4`：`ScanFilter.setServiceUuid(0xFFF0)` 的确切位置与影响 |
| H6 | `feature/.../ble/ProtocolParser.kt` | 核实 `crc8()` 是否存在、算法参数是否与 R4 §1 **逐位一致** |
| H7 | `core/database/`（schema + migrations） | 核实 `O-5`（`anomaly_events` 缺 `userId`）、`D6`（`fallbackToDestructiveMigration`）、`D4`（`vitals_samples` 是否已存在） |

> **读取纪律**：逐文件读完才能在报告 §1 标记「已读」；**读不到的必须写明原因**，不允许跳过后仍声称已读。

---

## 2. 十条红线（违反即回滚，且会在报告验收中被一票否决）

1. **协议只读**：禁止修改 `docs/PROTOCOL.md` / `docs/PROTOCOL_VECTORS.md` / `protocol-pin.json` 的内容；
   唯一允许是 **Pulse 侧发起的整文件替换，且替换后 byte-identical**。
2. 🚫 **禁止实现 `0xFFF5`（时间同步）与 `0xFFF6`（体征上传）**——须 D1 / D4 决策 + 协议升版 + 新增向量之后。
3. **不得修改既有向量的期望值**；实现与向量冲突 → **以向量为准**，修你方代码，并登记上报。
4. **任何 `.md` 不得复制协议版本号与 SHA-256**（数值只存在于 `protocol-pin.json`）。
5. **不得引入网络能力**（无 `INTERNET` / WiFi / 云）。
6. **不得修改 TianshangPulse 仓库任何文件**（对你是只读镜像来源）。
7. **不得替用户拍板 D1–D6**（只给建议，标明「建议，不代拍板」）。
8. **不得声称「实机已验证」**——当前无硬件；无硬件支撑的结论一律标注 **「未在实机验证」**。
9. 写类型约束：`0xFFF1` / `0xFFF3` / `0xFFF4` 必须 `WRITE_TYPE_DEFAULT`，**禁止 `WRITE_TYPE_NO_RESPONSE`**；CCCD 写入必须串行。
10. **不要把向量期望字节复制进你的文档**（按向量 **ID** 引用即可），避免出现第二份真相源。

---

## 3. 你的任务范围（第一批 · 连通层 · Health 侧）

任务编号取自 `protocol-pin.json` → `pending_decisions.scope`（**只引用编号，不复制估算数值**）：

| 编号 | 内容（定义以 H2 任务卡为准） |
| --- | --- |
| **D6** | 移除 Room `fallbackToDestructiveMigration`，改用显式 Migration |
| **T0 – T3** | 连接与扫描层改造：**O-4 放宽扫描过滤**、MTU 协商、CCCD 串行订阅、连接状态机 |
| **T5b** | 扫描/直连相关卡（以 H2 定义为准） |
| **T6（内联）** | 契约测试第一批**内联落地**（24 条向量全组断言，见 R4） |
| **T7** | 实现卡（以 H2 定义为准） |
| **T11** | 收尾卡（以 H2 定义为准） |
| **本文新增前置** | ① 若缺失则创建 `feature/watch` 模块（H3 核实）；② 按 `O-6` 精准放行 `.gitignore` 中治理文档与契约文件；③ README 增列 BLE 所需权限并声明**不需要 `INTERNET`** |

> 若 H2 与本文冲突：**以 H2 + R4 向量为准**，并在报告 §4 指出差异。

---

## 4. 《交接报告》规格 —— 你的必交付物

### 4.1 文件要求

| 项 | 要求 |
| --- | --- |
| 文件名 | `HANDOFF_REPORT_HEALTH_SIDE.md` |
| 位置 | TianshangHealth 仓库**根目录** |
| 编码 / 行尾 | UTF-8 **无 BOM**；LF |
| 入库 | 必须提交进 git（若被 `O-6` 的 `.gitignore` 挡住，**先放行再提交**——报告进不了 git 视为未交付） |
| 语言 | 中文；术语保留原文（UUID、MTU、CCCD、GATT 等） |

### 4.2 必含章节（9 节，缺任一节即视为未完成）

| # | 章节 | 必须写明的内容 |
| --- | --- | --- |
| 1 | **已读清单** | R1–R9（9 项）与 H1–H7（7 项）逐项：路径 / 是否读到 / **一行核心结论**；未读项必须给原因。目标覆盖率 **100%** |
| 2 | **仓库现状核查** | ① 模块列表（`feature/watch`、`core/pulse-protocol` 是否存在）；② `BleManager.kt` 过滤代码**确切位置与原文**；③ `ProtocolParser.ktcrc8()` 是否存在 + 算法参数；④ `.gitignore` 实际规则；⑤ Room 迁移链与 `anomaly_events` 是否含 `userId` |
| 3 | **协议一致性自查** | 对照 R4 的 **24 条向量逐条**标注：✅ 已一致 / ⚠️ 未实现 / ❌ 实现冲突（附**代码位置**与差异描述）。**不允许**「整体符合」这类模糊结论 |
| 4 | **矛盾与待上报项** | 你发现的「文档 vs 代码」「两仓不一致」等，逐条给：现象 / 证据 / 影响 / 建议归属；并**明确写「未自行修改真源」** |
| 5 | **D1–D6 影响评估** | 每项：对 Client 的影响 + 你的**建议**（须标注「建议，不代拍板」） |
| 6 | **任务执行状态** | D6 / T0–T3 / T5b / T6 / T7 / T11 **逐卡**：状态、改动文件、**验收命令与输出摘要** |
| 7 | **测试证据** | 契约测试：跑了几条、通过几条、失败明细；单元/主机测试的**命令 + 结果原文**（如 `./gradlew :feature:watch:test`） |
| 8 | **边界声明** | ① 「未在实机验证」声明；② `git status` 结果清单（**改了哪些 / 明确未改哪些**，尤其要证明三件套与 Pulse 仓未被触碰） |
| 9 | **下一步与阻塞** | 需要 Pulse 侧做什么（**按 R1 §3.3 上报格式**：模块 / 现象 / 复现 / 预期 / 实际 / 环境）；需要用户拍板什么 |

### 4.3 质量门槛（我方按此验收，任一不达标打回）

| # | 门槛 |
| --- | --- |
| G1 | 必读清单覆盖 **100%**（缺项有理由） |
| G2 | **24 条向量逐条有结论**，无模糊表述 |
| G3 | 报告中 **0 个**协议版本号、**0 个** 64 位十六进制串 |
| G4 | 每条「已验证」结论都附**命令与输出**；无硬件支撑的一律标「未在实机验证」 |
| G5 | 报告已 **commit** 且可在你方仓库 `git log` 中查到 |
| G6 | §8 中明确列出**未修改**的文件清单（含三件套与 Pulse 仓） |

---

## 5. 回传方式

1. 将 `HANDOFF_REPORT_HEALTH_SIDE.md` 置于你方仓库根目录并 **commit**（必要时先按 `O-6` 放行 `.gitignore`）；
2. **把该文件全文回传给用户**，由用户转交 TianshangPulse 侧核对；
3. 三件套（`PROTOCOL.md` / `PROTOCOL_VECTORS.md` / `protocol-pin.json`）只接受 **Pulse 侧发起的整文件替换**；
   替换动作必须在报告 §8 中留痕（替换前后时间、文件哈希核对结论——**哈希值本身仍不写进 `.md`**，只写「一致/不一致」）；
4. 若报告因 `.gitignore` 无法入库，改为在回传正文中附**全文**，并把放行 `.gitignore` 列为 P0 阻塞项上报。

---

## 6. 建议执行顺序

| 步 | 动作 | 产出 |
| --- | --- | --- |
| 1 | 读 R1 → R2 → R3 → R4（先立协议与规则约束） | 建立裁决顺序认知 |
| 2 | 读 R5（接口说明书）→ R6（任务卡） | 明确接口与任务 |
| 3 | 读 H1 → H2（你方规矩与任务卡） | 对齐验收口径 |
| 4 | 读 H3 → H7（现状核查） | 报告 §1、§2 |
| 5 | 读 R7 → R8（实现与测试对照） | 报告 §3、§4 |
| 6 | 执行 D6 / T0–T3 / T5b / T6 / T7 / T11 | 报告 §6、§7 |
| 7 | 写报告 → 核对 §4.3 门槛 → commit → 回传 | 交付完成 |

---

*由 TianshangPulse 侧 Agent 生成 · 接口说明书：`HANDOFF_TIANSHANGHEALTH.md` · 对端同名计划：`PLAN_WATCH_INTEGRATION.md`*
*状态：⏳ 待对接 Agent 执行并交付《交接报告》· 全项目**未在实机验证**（硬件在途）*
