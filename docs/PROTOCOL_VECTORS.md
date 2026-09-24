# TianshangPulse 协议契约向量（Golden Vectors）

> **真源**：`TianshangPulse` 仓库 `docs/PROTOCOL_VECTORS.md`。
> 本文件是**双仓库同步副本**：`TianshangHealth` 仓库 `docs/PROTOCOL_VECTORS.md` 必须与本文件 **byte-identical**，
> 完整性由 `protocol-pin.json` 的 `lock_files[].value`（SHA-256）锁定。
> 任何修改必须先在 Pulse 侧实施 → 升协议版本 → 重算 SHA-256 → 整体复制到 Health 侧 → 同步更新两份 `protocol-pin.json`。
> **禁止在任一仓库手工编辑本文件。**

| 项 | 值 |
| --- | --- |
| 状态 | **FROZEN** |
| 协议版本 | 由 `protocol-pin.json`.`protocol_version` 锁定（数值真源只在该文件，勿在本文件复制） |
| 覆盖范围 | `docs/PROTOCOL.md` §1–§6 现行线格式（`0x2A37`/`0x2A5F`/`0xFFF1`–`0xFFF4`） |
| 生成方式 | 由固件真实源码 `firmware/main/ble/gatt_server.c` 的 `ble_crc8()` / `ble_pack_anomaly()` **host 编译执行产出**，并经独立 Python 实现交叉验证——**非手工推算** |

---

## 1. 算法唯一定义（双端必须逐位一致）

```
crc = 0x00
for each byte b in payload[0 .. n-1]:
    crc ^= b
    repeat 8 times:
        crc = (crc & 0x80) ? (((crc << 1) ^ 0x07) & 0xFF) : ((crc << 1) & 0xFF)
```

| 属性 | 值 |
| --- | --- |
| 多项式 | `0x07` |
| 初值 | `0x00` |
| 反射 | 无（MSB-first，输入/输出均不反转） |
| 最终异或 | 无 |
| 覆盖范围 | 载荷**除末尾 CRC 字节外**的全部字节 |
| 字节序 | Little Endian（多字节整数低字节在前） |

参考实现（必须保持等价）：
- 固件：`firmware/main/ble/gatt_server.c::ble_crc8()`（49 行起）
- APP：`feature/watch/src/main/java/com/tianshang/health/feature/watch/ble/ProtocolParser.kt::crc8()`

---

## 2. 向量 V1 — CRC-8 基础

| ID | 输入（hex） | 期望 CRC-8 |
| --- | --- | --- |
| V1 | `ff` | `f3` |
| V1b | `01` | `07` |
| V1c | `12 34` | `f1` |

---

## 3. 向量 V2–V5b — 异常事件 `0xFFF2`（11 字节，Notify）

线格式：`type[1] | timestamp_ms[8 LE] | confidence[1] | crc8[1]`，CRC 覆盖**前 10 字节**。

| ID | 场景 | type | timestamp_ms | confidence | 期望 11 字节（hex） |
| --- | --- | --- | --- | --- | --- |
| V2 | 全零边界（**trap**：CRC=`00` 是合法结果，不得当作错误丢弃） | 0 | 0 | 0 | `00 00 00 00 00 00 00 00 00 00 00` |
| V3 | SpO2 异常 | 1 | 1700000000000 | 92 | `01 00 68 e5 cf 8b 01 00 00 5c 21` |
| V4 | AF 异常 | 3 | 1750000000000 | 88 | `03 00 dc 20 74 97 01 00 00 58 fb` |
| V5 | 模型切换事件（ts 极小值） | 2 | 4095 | 0 | `02 ff 0f 00 00 00 00 00 00 00 3e` |
| V5b | 边界 confidence=0 | 1 | 1700000000100 | 0 | `01 64 68 e5 cf 8b 01 00 00 00 46` |

解析断言（以 V3 为例）：`typeCode=1`、`timestampMs=1700000000000`、`confidence=92`、`valid=true`。

时间戳参考（`uint64` LE，8 字节）：

| 值 | 十六进制 | 含义 |
| --- | --- | --- |
| 1700000000000 | `0x0000018BCFE56800` | 2023-11-14T22:13:20Z |
| 1750000000000 | `0x000001977420DC00` | 2025-06-15T15:06:40Z |
| 0 | `0x0000000000000000` | 1970-01-01T00:00:00Z（**不得**作为"未同步"的展示值） |

> `timestamp_ms` 语义必须是 **Unix epoch 毫秒**，不是"开机以来毫秒"。见 §9 与 `protocol-pin.json` → `open_items` O-2。

## 4. 向量 V6 — 用户配置 `0xFFF1`（8 字节，WRITE with response）

线格式（**必须 ≥ 8 字节**；固件 `len < 8` 直接返回 `ATT_ATTR_VALUE_LEN`）：

```
hr_thr[2 LE] | spo2_thr[1] | gender[1] | age[2 LE] | reserved[1] = 0x00 | crc8[1]
```

CRC 覆盖**前 7 字节**（含 reserved 字节）：`crc8(buf, 7) == buf[7]`。

| ID | 场景 | 期望 8 字节（hex） |
| --- | --- | --- |
| V6 | hr=100, spo2=92, gender=0(女), age=25 | `64 00 5c 00 19 00 00 65` |
| V6b | hr=200, spo2=90, gender=1(男), age=30 | `c8 00 5a 01 1e 00 00 9a` |
| V6c | 全零边界（合法，CRC=`00`） | `00 00 00 00 00 00 00 00` |
| V6d | hr=65535, spo2=100, gender=1, age=65535 | `ff ff 64 01 ff ff 00 22` |

反例（**必须被拒绝**，用于双端负向测试）：

| ID | 载荷（hex） | 期望行为 |
| --- | --- | --- |
| P6a | `64 00 5c 00 19 00 17`（7 字节，缺 reserved） | Server 拒绝（`ATT_ATTR_VALUE_LEN`）；Client 不得构造该长度 |
| P6b | `64 00 5c 00 19 00 00 64`（CRC 错） | Server 拒绝（`ATT_INVALID_PDU`），配置不生效 |

Client 侧断言：`buildUserConfigPacket(...)` 长度恒为 8，且 `crc8(pkt, 0, 7) == pkt[7]`。

---

## 5. 向量 V7 — 模型切换 `0xFFF3`（2 字节，WRITE with response）

| ID | 场景 | 期望字节（hex） |
| --- | --- | --- |
| V7 | 切到模型 B | `01 07` |
| V7b | 回退默认模型 A | `00 00` |
| P7 | `02 05`（idx > 1） | Server 拒绝（`ATT_INVALID_PDU`） |

---

## 6. 向量 V8 — 离线批量 `0xFFF4`

Read 返回 = **N × 11 字节**（每条结构见 §3，自带 CRC-8），`N ≤ 20`，`N = len / 11`；**不追加整批 CRC**。

| ID | 场景 | 构造规则 | 期望 |
| --- | --- | --- | --- |
| V8a | 3 条 | type=1, ts=`1700000000000 + i*1000`, conf=`80 + i`, i=0..2 | 33 字节；`ev0 = 01 00 68 e5 cf 8b 01 00 00 50 05`、`ev2 = 01 d0 6f e5 cf 8b 01 00 00 52 d9` |
| V8b | 满批 20 条 | type=3, ts=`1750000000001 + i*60000`, conf=`61 + i`, i=0..19 | 220 字节；`ev0 = 03 01 dc 20 74 97 01 00 00 3d be`、`ev19 = 03 21 41 32 74 97 01 00 00 50 92` |

**已证实结论（勿重复踩坑）**：

1. `crc8(msg)`（`msg` 为**完整载荷，含其自身 CRC 字节**）恒等于 `0x00`。实测样本：`01`、`12 34`、V3/V4/V5 的 11 字节、V8a/V8b 整批——**全部 `0x00`**。原因见 O-1。
2. 因此"整批末尾追加 CRC-8"**零信息量，不采用**。
3. 正确性依赖逐条 CRC + 长度整数倍校验；**任一条坏包只丢弃该条**，不得整批丢弃。

Client 侧断言：

- `11 <= len <= 220` 且 `len % 11 == 0`，否则整批拒绝并记日志；
- 逐条解析，`valid=true` 才落库；
- 落库完成后向 `0xFFF4` 写入 **1 字节 `0x01`** 作为 ACK（Server 按**上一次 Read 实际交付条数**精确 pop，与 ACK 内容无关）；
- Read 返回 0 字节 = 无缓存，**不写 ACK**；
- MTU 不足导致服务端截断时，`len` 仍为 11 的整数倍（可能 < 220），Client 必须按 `len` 处理而不能假定 20 条。

---

## 7. 向量 V9 — 坏 CRC 必须被拒绝

| ID | 场景 | 载荷（hex） | 期望行为 |
| --- | --- | --- | --- |
| V9 | V3 末字节取反 | `01 00 68 e5 cf 8b 01 00 00 5c de` | `valid=false`；不落库、不弹通知 |
| V9b | 短包（10 字节） | `01 00 68 e5 cf 8b 01 00 00 5c` | 解析返回 `null`（长度不足） |

---

## 8. 向量 V10 — 标准特征值载荷解析

| ID | 特征值 | 输入（hex） | 期望结果 |
| --- | --- | --- | --- |
| V10 | `0x2A37` | `01 00` | `1` |
| V10b | `0x2A37` | `ff ff` | `65535` |
| V10c | `0x2A37` | `3c`（仅 1 字节） | `null`（长度不足，**不得抛异常**） |
| V10d | `0x2A5F` | `64` | `100` |
| V10e | `0x2A5F` | `00` | `0` |
| V10f | `0x2A5F` | 空数组 | `null` |

已知线格式偏离（见 O-3，**有意保留，禁止单端更改**）：`0x2A37` 无 flags 字节；`0x2A5F` 非标准 PLX 结构。

---

## 9. 未纳入本文件锁定范围

- 计划中的 `0xFFF5`（时间同步）与 `0xFFF6`（体征上传）：必须在 **D1/D4 决策 + 协议升版后**新增向量并更新 `protocol-pin.json`，**在此之前任何一端都不得实现**。
- `type=2`（模型切换通知）与 `type=3`（AF 异常）的**业务语义**（展示、告警分级、落库字段映射）不属于线格式，由 Client 语义层定义。

---

## 10. 双端执行方式

| 仓库 | 角色 | 实现位置 | 命令 |
| --- | --- | --- | --- |
| TianshangPulse | Server | `tests/host/test_protocol.c`（新增；`tests/host/stubs/` 已有 FreeRTOS / esp_log 桩） | 见 `PLAN_WATCH_INTEGRATION.md` Phase 4.6 任务卡 |
| TianshangHealth | Client | `core:pulse-protocol` 模块 `ProtocolCodecTest.kt`（纯 JVM，无 Android 依赖） | `./gradlew :core:pulse-protocol:test` |

要求：两侧对 V1–V10f **全组**断言，同一 ID 的期望字节**完全相同**；本向量表是唯一裁决依据——代码与向量冲突时，以向量为准并立刻修代码。

---

## 11. 变更历史

| 版本 | 日期 | 变更 | 影响 |
| --- | --- | --- | --- |
| 1.0.0 | 2026-09-20 | 首次冻结：V1–V10f（24 条断言）+ O-1..O-4 记录 | 双端首轮接线基线 |

---

*维护：TianshangPulse（Server）· 变更流程见 `protocol-pin.json` → `sync_rules`*

