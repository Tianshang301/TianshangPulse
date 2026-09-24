# PLAN_TianshangPulse — BLE 配套 APP 开发计划（与 TianshangHealth 同步）

> ⚠️ **本文档为 2026-09-08 的初始规划，已被 [`PLAN_WATCH_INTEGRATION.md`](./PLAN_WATCH_INTEGRATION.md) 取代（2026-09-20）**，保留作为历史记录。
> 已被证伪、**勿再照此实现**的内容：§4「固件 GATT Server 已按协议实现，本轮 APP 开发无需固件改动」——实测发现固件侧 3 个 P0 缺陷（广播载荷未配置 / 时间戳为开机 ms / `PROTOCOL.md` §6 与实现矛盾）。

> **状态：📋 规划中（待审批）**
>
> 本文件是 [TianshangHealth](https://github.com/Tianshang301/TianshangHealth) 仓库 `PLAN_TianshangHealth.md` 的**配对同步文档**，承载两侧仓库的协调计划。
> 协议唯一来源：`docs/PROTOCOL.md`（手表端定义，APP 端遵循）。
> 任何协议变更走 AGENTS.md §3.2 协议变更流程。

---

## 1. 项目关系

| 层 | 仓库 | 角色 |
|---|---|---|
| 固件 | `TianshangPulse` | **GATT Server** — 定义协议，提供数据 |
| APP | `TianshangHealth` | **GATT Client** — 连接手表，消费数据，发送配置 |

---

## 2. 同步目标

为 TianshangPulse 智能手表开发 Android 配套 BLE 功能，**集成进现有 TianshangHealth APP**（非独立新应用），保持单一 APK。

**已确认决策**（两侧一致）：

| 项 | 决策 |
|---|---|
| 集成方式 | 集成进现有 APP，新建 `feature:watch` 模块 |
| 入口 | Dashboard 快捷入口 + 侧边路由 `watch`，不改底部导航 |
| 本轮范围 | **P1 连接 / P2 实时 HR·SpO2 / P3 AF 异常事件** |
| 延后范围 | P4 用户配置（`0xFFF1`）、P5 离线批量同步（`0xFFF4`）、P6 模型切换（`0xFFF3`） |
| minSdk | 保持 24（BLE 于 API 24 可用；API 31+ 运行时权限） |

---

## 3. APP 端实施计划（本轮 P1–P3）

### 3.1 新模块 `feature:watch`

```
feature/watch/src/main/java/com/tianshang/health/feature/watch/
├── ble/
│   ├── BleConstants.kt          # 8 个 UUID（0x180D/0x2A37/0x2A5F/0xFFF0-FFF4）
│   ├── ProtocolParser.kt        # CRC-8 + 解析 HR(uint16 LE)/SpO2(uint8)/AF 事件
│   └── BleManager.kt            # 扫描(过滤"TianshangPulse"/0xFFF0)→连接→发现服务→
│                                #   校验 6 特征值→指数退避重连(1→2→4→8s,上限30s)
│                                #   StateFlow<BleConnectionState> + requestMtu(512)
├── data/
│   └── WatchRepository.kt       # 包装 AnomalyEventDao，Flow 暴露历史事件
├── service/
│   ├── WatchBleService.kt       # LifecycleService 前景服务（维持 GATT 连接）
│   └── AfNotificationHelper.kt  # AF 高优先级通知（后台也可弹窗）
├── ui/
│   ├── WatchViewModel.kt        # sealed class WatchUiState
│   └── WatchScreen.kt           # 连接状态 + 实时 HR/SpO2 + AF 横幅 + 历史列表
└── di/
    └── WatchModule.kt           # Hilt 绑定 BleManager / WatchRepository
```

### 3.2 数据层（`core:database`）

- 新增 `AnomalyEvent` Room 实体（加密 SQLCipher 存储）+ `AnomalyEventDao`
- `TianshangDatabase` version 1→2，`MIGRATION_1_2` 非破坏建表（避免清空用户数据）

### 3.3 权限与 Manifest

- `BLUETOOTH_SCAN` + `BLUETOOTH_CONNECT`（API 31+，`usesPermissionFlags="neverForLocation"`）
- 遗留 `BLUETOOTH`/`BLUETOOTH_ADMIN`（`maxSdkVersion=30`）
- `FOREGROUND_SERVICE` + `FOREGROUND_SERVICE_CONNECTED_DEVICE`（targetSdk 35 强制）
- `<uses-feature android:name="android.hardware.bluetooth_le" android:required="true"/>`
- 注册 `WatchBleService`（`foregroundServiceType="connectedDevice"`）

### 3.4 协议解析（对齐 `docs/PROTOCOL.md`）

- `crc8()`：多项式 `0x07`，初值 `0x00`
- `parseHeartRate(bytes)` → BPM（uint16 LE）
- `parseSpO2(bytes)` → %（uint8）
- `parseAnomalyEvent(bytes)` → type 3=AF，校验 CRC-8，丢弃无效包
- `0xFFF1`/`0xFFF3`/`0xFFF4` 打包/拆包函数本轮**不实现**（P4-P6 延后）

### 3.5 多语言

- 新增 ~25 键 × 21 语言（UTF-8），`check_strings.py` 验证

### 3.6 测试

| 测试 | 类型 | 覆盖 |
|---|---|---|
| `ProtocolParserTest` | JVM 单测 ~12 用例 | CRC-8 已知向量、HR LE、SpO2、AF type=3 有效/无效 CRC、短包 |
| `WatchRepositoryTest` | Mock DAO | 委托与 Flow 发射 |
| `WatchViewModelTest` | Mock repo + `StandardTestDispatcher` | 状态切换、AF 事件注入 |
| `MigrationTest` | androidTest | `MIGRATION_1_2` 非破坏建表 |

---

## 4. 固件端配合要求（验证点）

> 固件 GATT Server 已按 `docs/PROTOCOL.md` 实现，本轮 APP 开发无需固件改动，仅需配合端到端验证。

| 验证点 | 固件行为 | APP 行为 |
|---|---|---|
| 服务发现 | 注册 `0x180D` + `0xFFF0` 两服务、6 特征值 | `discoverServices()` 后校验全部 UUID |
| 实时 HR | `0x2A37` Notify，`uint16_t bpm` | 显示 BPM |
| 实时 SpO2 | `0x2A5F` Notify，`uint8_t pct` | 显示 % |
| AF 异常 | `0xFFF2` Notify，type=3，CRC-8 附尾 | 校验 CRC → Room 持久化 → 弹窗/通知 |
| 后台持续监测 | 保持连接（MTU 512，`requestMtu` 协商） | `WatchBleService` 前景服务常驻 |

**联调前提**：实物手表（TianshangPulse 固件，`CONFIG_SENSOR_SIM_PPG=y` / `CONFIG_SENSOR_SIM_PPG_AF=y` 模拟模式）。

---

## 5. 端到端验证流程（联调 Checklist）

连接手表后执行：

1. **服务发现**：校验 6 个特征值 UUID 均已注册
2. **实时 HR/SpO2**：安静环境下收到 HR ~60-72bpm（窦性模拟）、SpO2 数值
3. **AF 事件**：开启 `CONFIG_SENSOR_SIM_PPG_AF=y` 后收到 type=3 事件（含时间戳 + 置信度），APP 弹窗 + 历史列表落库
4. **CRC 校验**：构造坏包（篡改 CRC 字节）→ APP 丢弃且不落库
5. **断连重连**：手动断开 → 观察指数退避重连（1→2→4→8s，上限 30s）

---

## 6. 里程碑

| 里程碑 | 内容 | 依赖 |
|---|---|---|
| M1 — APP 侧开发完成 | `feature:watch` 模块 + DB + 权限 + 入口集成，单元测试通过 | 无（固件协议已定） |
| M2 — 单侧自测 | `ProtocolParserTest` / `WatchRepositoryTest` / `WatchViewModelTest` 全绿，`detektAll` 零 issue | M1 |
| M3 — 真机联调 | 实物手表端到端验证（§5 Checklist） | M1 + 实物手表 |

---

## 7. 风险与说明

- **无实物手表无法端到端验证**：解析器靠单测覆盖，BLE 流程需真机 + 固件联调
- **隐私权衡**：APP 不新增 `INTERNET` 权限，离线原则保持；仅新增蓝牙本地连接权限
- **AF 后台弹窗依赖前景服务**：`WatchBleService` 常驻维持 GATT 连接
- **P4-P6 延后**：`0xFFF1` 配置、`0xFFF4` 批量同步、`0xFFF3` 模型切换按协议预留，后续按需扩展

---

*初始版本：2026-09-08*
*同步自：TianshangHealth/PLAN_TianshangHealth.md*
*配套固件版本：TianshangPulse（`docs/PROTOCOL.md` 现行版本）*
