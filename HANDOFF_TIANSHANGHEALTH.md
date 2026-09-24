# TianshangPulse → TianshangHealth 接线交接文档

> **文档性质**：面向 **GATT Client（TianshangHealth / Android APP）** 的接口与数据通信协议交接说明，
> 覆盖「怎么扫到设备 → 怎么连 → 每个特征值怎么收发 → 怎么验证 → 什么还没做」。
> **本文件不是协议真源**，真源与冲突裁决顺序见 §2；本文件与真源冲突时**一律以真源为准**并请回报修改。
> **本文件不含协议版本号与 SHA-256**（按 AGENTS.md §9.1「数值唯一性」，该两类数值只允许出现在 `protocol-pin.json`）。
> **状态图例**：✅ 已实现并经主机侧验证 ｜ ⏳ 接口存在但**未在实机验证** ｜ 🚫 已知缺陷/待决，**禁止提前实现**
> **当前状态**：✅ 已完成 Server 侧核查「问题 1–7」的**文档修订**（明细见文末「修订记录」）；
> ✅ 已按 Health 侧第一批交付**回填**（§10 与 §13.6 的模块/权限前置已按实际状态更新，§12.6 的镜像已执行）；
> ⏳ 全文**仍待实机联调**。
>
> **两道独立闸门**（互不依赖，勿混为一谈）：
> ① **登记闸门**：✅ **已关闭**（2026-09-24）——D-A / D-B / D-C / D-D 已登记进 `protocol-pin.json.open_items`
>（仅元数据，未动 `lock_files` / `protocol_version` / `pinned_at` / `status`，未重算哈希、未升版）。
> ⏳ **解决**（收紧 `PROTOCOL.md` §3/§5、固件补 CRC 校验与 `ble_notify_spo2` 接线等）仍属所有者待办，见 §8.2。
> ② **基线闸门**：本文档曾对 Health 侧现状滞后描述（§10、§13.6 初版），现已回填；
> 但 §11 的**第二批任务仍被 D1–D6 未拍板与 Pulse 侧未升版所阻塞**，该部分此前「不可执行」的判断依然成立。

---

## 1. 角色与责任边界

| 项 | TianshangPulse（定义方） | TianshangHealth（接收方） |
| --- | --- | --- |
| BLE 角色 | **GATT Server** | **GATT Client** |
| 改动顺序 | **先改**（含升版、重算 pin、整体复制） | **后跟**（不得反向定义或修改协议） |
| 产物 | `docs/PROTOCOL.md`（线格式定义）<br>`docs/PROTOCOL_VECTORS.md`（契约向量，**唯一裁决**）<br>`protocol-pin.json`（版本 / 哈希 / 开放项 / 待决项） | 三份**只读镜像** + Client 实现 + 契约测试 |
| 源码位置 | `firmware/main/ble/gatt_server.c` | `feature/.../ble/` + `core:pulse-protocol` |

**单向契约**：Server 定义、Client 遵循。Client 发现协议问题 → 上报 Pulse → Pulse 走六步流程
（AGENTS.md §9.2）后**整体复制**到本仓库；Health 侧**禁止**直接改协议定义或向量。

---

## 2. 真源与冲突裁决

| 优先级 | 文件 | 作用 |
| --- | --- | --- |
| ① **最终裁决** | `TianshangPulse/docs/PROTOCOL_VECTORS.md` | 24 条黄金向量（V1–V10f）；**代码与向量冲突时以向量为准，立刻修代码** |
| ② 线格式定义 | `TianshangPulse/docs/PROTOCOL.md` | 字段布局、UUID、属性、语义 |
| ③ 数值 / 哈希 / 开放项 | `protocol-pin.json` | 协议版本、两个 SHA-256、`wire_constraints`、`open_items`、`pending_decisions` |
| ④ 本文件 | `HANDOFF_TIANSHANGHEALTH.md` | 交接指引与实施建议（**非裁决**） |

**三份文件必须双仓 byte-identical**：`PROTOCOL.md`、`PROTOCOL_VECTORS.md`、`protocol-pin.json`。

**校验方式**：

1. 比对两仓 `protocol-pin.json` 是否 byte-identical；
2. 计算各自 `PROTOCOL.md` 与 `PROTOCOL_VECTORS.md` 的 SHA-256，核对是否等于 `protocol-pin.json` → `lock_files[].value`。

> 协议版本号与哈希的**数值**只在 `protocol-pin.json` 中出现（`protocol_version` / `next_protocol_version` / `lock_files[].value`），
> 任何 `.md` 均不复制其具体值。

---

## 3. 设备发现与连接（Client 第一步，含 P0 阻塞项）

### 3.1 设备标识

| 项 | 值 |
| --- | --- |
| 设备名（GAP） | `TianshangPulse` |
| 自定义服务 UUID | `0xFFF0` |
| 标准心率服务 UUID | `0x180D` |
| 地址类型 | Public（`BLE_OWN_ADDR_PUBLIC`） |
| 广播模式 | Undirected + General Discoverable，FAST 间隔，`BLE_HS_FOREVER`（永久广播） |
| 最大连接数 | **1**（`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`） |
| 配对 / 加密 | **当前未配置 Security Manager → 无绑定、无加密**（待决 D3；未定前链路为明文） |
| BLE 栈 | NimBLE（host + controller），`CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512` |

### 3.2 🚫 P0 阻塞项：扫描过滤必须放宽（`protocol-pin.json` → `open_items.O-4`）

| 侧 | 现状 | 要求 |
| --- | --- | --- |
| **Server（Pulse，先改）** | `start_advertising()` **未调用** `ble_gap_adv_set_fields` / `ble_gap_adv_rsp_set_fields`；全固件 `uuids16` 出现 **0 次** → 广播里**没有** `0xFFF0`，也没有显式 flags 字段 | 显式设置 flags + `0xFFF0`（可带 `0x180D`）+ 设备名，并检查返回值打日志 |
| **Client（Health，兼容）** | `BleManager.kt` 使用 `ScanFilter.setServiceUuid(0xFFF0)` 做**硬件级过滤** → 当前**扫不到设备** | 🚫 Server 修复前**不得设置任何硬件级 ScanFilter**（含 `setServiceUuid` 与按名称过滤）；**扫描全部 BLE 设备**，**连接后**通过服务发现校验 `0x180D` 与 `0xFFF0`；`deviceName` / 广播中的 `0xFFF0` **仅作辅助提示，缺失不算失败** |

**结论**：广播载荷当前**既无 `0xFFF0`，也不保证含设备名**——`ble_svc_gap_device_name_set()` 只设置 GAP 设备名特征，**不会写入广播载荷**，而 `start_advertising()` 又未调用 `ble_gap_adv_set_fields`。因此 Server 修复前 Client **不设任何过滤**、以**连接后的服务发现为唯一判据**；`0xFFF0` / `deviceName` 只能作辅助，不能作准入。此项为 **Phase 1 阻塞项，两端同时改**。

### 3.3 连接协商与传输约束

| 项 | Server 现值 | Client 必须怎么做 |
| --- | --- | --- |
| MTU | preferred = 512（`KBleMtuSize=512`） | **必须** `requestMtu(512)`；ATT 有效载荷上限 = MTU − 3 |
| 收满一批 20 条所需 MTU | 20 × 11 = 220 字节 | 需 **MTU ≥ 223**；不足时服务端按 MTU 截断（长度仍是 11 的整数倍） |
| 写类型 | `0xFFF1` / `0xFFF3` / `0xFFF4` 固件仅声明 `PROP_WRITE`（with response） | **必须** `WRITE_TYPE_DEFAULT`；**禁止** `WRITE_TYPE_NO_RESPONSE`，否则写入会被拒 |
| CCCD `0x2902` | 由 NimBLE 自动注册 | `getDescriptor(0x2902)` 可取得；**CCCD 写入必须串行**（逐个写并等待回调再写下一个） |
| 连接数量 | 仅支持 1 连接 | 用完必须主动断开，否则手表无法被其他客户端连接 |

### 3.4 GAP 事件对 Server 行为的影响（Client 可观测）

| GAP 事件 | Server 行为 |
| --- | --- |
| CONNECT 成功 | 记录 `conn_handle`；`power_set_mode(POWER_MODE_ACTIVE)`（连接即全速） |
| CONNECT 失败 | 重新开始广播 |
| DISCONNECT | 清空 `conn_handle`；`power_set_mode(POWER_MODE_LIGHT_SLEEP)`（断开即降频浅睡）；**立即重新广播** |
| ADV_COMPLETE | 自动重新广播 |

**推论**：

1. 未连接时三个 Notify（`0x2A37` / `0x2A5F` / `0xFFF2`）**不会发出**——Server 以
   `conn_handle == BLE_HS_CONN_HANDLE_NONE` 判定并返回 `ESP_ERR_INVALID_STATE`；
2. **没有通知队列**：未连接期间产生的异常事件走**离线缓存**（§6.6），连接后靠批量读补回；
3. 断开后手表**立即**重播广播，Client 可直接重连，无需等广播超时。

### 3.5 Android 权限与扫描前置条件（Client 必须先满足）

| 场景 | 必需权限 / 前置 | 说明 |
| --- | --- | --- |
| Android 12+（API 31+） | `BLUETOOTH_SCAN`、`BLUETOOTH_CONNECT` | 运行时申请；若声明 `android:usesPermissionFlags="neverForLocation"`，则**必须移除 `ACCESS_FINE_LOCATION`**（两者同时存在会抛 `SecurityException`） |
| Android 11 及以下（API ≤ 30） | `ACCESS_FINE_LOCATION`（+ 传统 `BLUETOOTH` / `BLUETOOTH_ADMIN`） | 仅 `ACCESS_COARSE_LOCATION` 在多数机型**不足以返回扫描结果** |
| 所有版本 | **蓝牙已开启** | 未开启时 `startScan` 直接失败 |
| Android 6 – 11 | **位置服务已开启** | 位置开关关闭时扫描结果为空（系统级限制，**不是权限问题**） |

**扫描参数建议**：

- `ScanSettings.SCAN_MODE`：bring-up 阶段用 `SCAN_MODE_LOW_LATENCY`；
- `setReportDelay(0)`：关闭批量上报，保证回调及时；
- `callbackType = CALLBACK_TYPE_ALL_MATCHES`：配合 §3.2「不设过滤」，**不要**用 `ONLY_MATCHES` + 过滤器；
- 权限被拒时给出设置页跳转引导，**不要静默失败**；
- 本功能全程离线，**不需要 `INTERNET` 权限**，不要为 BLE 增加网络权限。

---

## 4. 服务与特征值总表

| 父服务 | 特征值 | 属性 | 方向 | 固定长度 | 逐包 CRC | 写类型 / 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `0x180D` Heart Rate | `0x2A37` | Notify | Watch → APP | **2** | ❌ 无 | 实时心率 `uint16` LE BPM（**无 flags 字节**） |
| `0x180D` Heart Rate | `0x2A5F` | Notify | Watch → APP | **1** | ❌ 无 | 实时血氧 `uint8` %（**非标准 PLX 结构**） |
| `0xFFF0` 自定义 | `0xFFF1` | Write | APP → Watch | **恒 8** | ✅ 覆盖前 7 字节 | `WRITE_TYPE_DEFAULT` |
| `0xFFF0` 自定义 | `0xFFF2` | Notify | Watch → APP | **恒 11** | ✅ 覆盖前 10 字节 | 异常事件（含 AF） |
| `0xFFF0` 自定义 | `0xFFF3` | Write | APP → Watch | **恒 2** | ✅ 覆盖第 0 字节 | `WRITE_TYPE_DEFAULT`（模型 A/B 切换） |
| `0xFFF0` 自定义 | `0xFFF4` | Read + Write | 双向 | Read: N×11（N≤20）<br>Write: ≥1 | Read 逐条自带；**无整批 CRC** | `WRITE_TYPE_DEFAULT`（批量同步 / ACK） |

**注册事实**（源自 `firmware/main/ble/gatt_server.c` 的 `gatt_db[]`）：

- `0x2A37` / `0x2A5F` / `0xFFF2` → `BLE_GATT_CHR_F_NOTIFY`；
- `0xFFF1` / `0xFFF3` → `BLE_GATT_CHR_F_WRITE`（**仅 with response**）；
- `0xFFF4` → `BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE`；
- 三个 Notify 的 CCCD 由 NimBLE 自动注册（Client 侧 `getDescriptor(0x2902)` 取得）。

**128-bit UUID 展开**（Android `UUID.fromString()` 可直接使用；16-bit UUID 按 Bluetooth SIG 标准基础 UUID `0000xxxx-0000-1000-8000-00805F9B34FB` 展开）：

| 16-bit | 128-bit |
| --- | --- |
| `0x180D` | `0000180d-0000-1000-8000-00805f9b34fb` |
| `0x2A37` | `00002a37-0000-1000-8000-00805f9b34fb` |
| `0x2A5F` | `00002a5f-0000-1000-8000-00805f9b34fb` |
| `0xFFF0` | `0000fff0-0000-1000-8000-00805f9b34fb` |
| `0xFFF1` | `0000fff1-0000-1000-8000-00805f9b34fb` |
| `0xFFF2` | `0000fff2-0000-1000-8000-00805f9b34fb` |
| `0xFFF3` | `0000fff3-0000-1000-8000-00805f9b34fb` |
| `0xFFF4` | `0000fff4-0000-1000-8000-00805f9b34fb` |
| `0x2902`（CCCD） | `00002902-0000-1000-8000-00805f9b34fb` |


---

## 5. 通用线格式

### 5.1 字节序

- **Little Endian（小端序）**：多字节整数低字节在前（如 `uint16` / `uint64`）；
- 单字节字段（`uint8`）无字节序问题；
- Android 侧建议统一用 `ByteBuffer.order(ByteOrder.LITTLE_ENDIAN)`。

### 5.2 CRC-8 唯一定义（双端必须逐位一致）

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
| 反射 | 无（MSB-first，输入 / 输出均不反转） |
| 最终异或 | 无 |
| 覆盖范围 | 载荷**除末尾 CRC 字节外**的全部字节 |
| 校验方向 | `crc8(前 n-1 字节) == 末字节` |

参考实现（**必须保持等价**）：

- 固件：`firmware/main/ble/gatt_server.c::ble_crc8()`
- APP：`feature/watch/src/main/java/com/tianshang/health/feature/watch/ble/ProtocolParser.kt::crc8()`

> ⚠️ **已证实结论（勿重复踩坑，对应 `open_items.O-1`）**：对**完整载荷（含其自身 CRC 字节）**
> 恒有 `crc8(msg) == 0x00`。因此「整批末尾再追加一个 CRC-8」**零信息量，不采用**。
> 正确性依赖**逐条 CRC** + **长度为 11 的整数倍**校验。

> 本节算法与 `docs/PROTOCOL_VECTORS.md` §1 必须始终一致；若冲突，**以向量文件为准**并立即回报。

---

## 6. 逐接口详述

### 6.1 `0x2A37` 实时心率（Notify，2 字节）

```
偏移  长度  类型       字段
0     2     uint16 LE  心率 BPM
```

| 项 | 说明 |
| --- | --- |
| CRC | 无 |
| flags 字节 | **无**（偏离 BLE 标准 Heart Rate Measurement，见 `O-3`，**禁止单端更改**） |
| 发送时机 | Server 每个 4 秒推理窗内有效峰数 ≥ 3 时发送一次（约 4 秒一次） |
| 空闲时 | 未连接 → 不发送（返回 `ESP_ERR_INVALID_STATE`） |

**Client 断言**（见 `PROTOCOL_VECTORS.md` §8）：V10 / V10b / V10c。
要求：长度不足时返回 `null` 并**记日志，不得抛异常**（V10c 为 1 字节输入）。

### 6.2 `0x2A5F` 实时血氧（Notify，1 字节）

```
偏移  长度  类型  字段
0     1     uint8 血氧饱和度 %
```

| 项 | 说明 |
| --- | --- |
| CRC | 无 |
| 结构 | **非标准 PLX Continuous Measurement**（见 `O-3`，禁止单端更改） |
| 合法范围 | 0–100 |
| 🚫 **当前实现状态** | `ble_notify_spo2()` **已定义但全固件 0 处调用** → **本接口暂无数据下发**（见 §8 的 D-C） |

**Client 断言**：V10d / V10e / V10f（正常值、0 值、**空数组返回 `null` 不抛异常**）。

### 6.3 `0xFFF1` 用户配置（Write，恒 8 字节）

```
偏移  长度  类型       字段
0     2     uint16 LE  心跳异常阈值 (BPM)
2     1     uint8      血氧异常阈值 (%)
3     1     uint8      性别 (0=女, 1=男)
4     2     uint16 LE  年龄
6     1     uint8      reserved（必须为 0x00）
7     1     uint8      crc8（覆盖偏移 0..6 共 7 字节）
```

| 项 | **协议要求（Client 必须满足）** | **当前固件行为（Server 待收紧，属 D-B）** |
| --- | --- | --- |
| 长度 | **恒 8 字节**（`wire_constraints.0xFFF1_min_length = 8`；向量 V6 期望恒 8） | 接受 **8–32 字节**：仅拒绝 `len < 8` 与 `len > 32`，**未强制 `len == 8`** |
| `reserved`（偏移 6） | **必须填 `0x00`** | **不校验该字节取值** |
| CRC | `crc8(pkt[0..6]) == pkt[7]` | 校验 `crc8(前 7 字节) == buf[len - 1]`（长度 > 8 时比的是**末字节**，中间字节被忽略） |
| ATT 错误码 | —（按协议构造即不会触发） | 长度非法 → `0x0D Invalid Attribute Value Length`；CRC 不符 → `0x04 Invalid PDU`（**配置不生效**） |
| 生效 | — | Server 解析后写入 `s_user_config` 并打印日志 |
| 🚫 下游 | — | `ble_get_user_config()` **当前无消费者** → 写入成功但**暂不改变手表行为**（见 §8 的 D-D） |

**Client 断言**（`PROTOCOL_VECTORS.md` §4）：

- 正向：V6 / V6b / V6c / V6d（覆盖正常值、全零边界、字段极值）；
- 反向：**P6a（7 字节，缺 reserved）必须被 Server 拒绝**；**P6b（CRC 错）必须被 Server 拒绝**；
- 构造器恒等式：`buildUserConfigPacket(...)` 长度恒为 8，且 `crc8(pkt, 0, 7) == pkt[7]`。

### 6.4 `0xFFF2` 异常事件（Notify，恒 11 字节）

```
偏移  长度  类型       字段
0     1     uint8      事件类型
1     8     uint64 LE  时间戳 timestamp_ms（Unix epoch 毫秒）
9     1     uint8      置信度 confidence（0–100）
10    1     uint8      crc8（覆盖偏移 0..9 共 10 字节）
```

| 字段 | 取值 |
| --- | --- |
| `type` | `0` = 心率异常 ｜ `1` = 血氧异常 ｜ `2` = 模型切换 ｜ `3` = **AF 异常（房颤检出）** |
| `timestamp_ms` | **必须**是 Unix epoch 毫秒；见 §7 的降级要求 |
| `confidence` | 0–100 |

**Client 断言**（`PROTOCOL_VECTORS.md` §3、§7）：

- 正向：V2（**全零边界 trap**：CRC 结果恰为 `00` 是**合法**的，不得当错误丢弃）、V3、V4、V5、V5b；
- 反向：V9（末字节取反 → `valid=false`，不落库、不弹通知）、V9b（10 字节短包 → 解析返回 `null`）。

**解析断言字段**：`typeCode`、`timestampMs`、`confidence`、`valid`。

### 6.5 `0xFFF3` 模型 A/B 切换（Write，恒 2 字节）

```
偏移  长度  类型  字段
0     1     uint8 模型索引（0=A 默认, 1=B）
1     1     uint8 crc8（覆盖偏移 0）
```

| 项 | **协议要求（Client 必须满足）** | **当前固件行为（Server 待收紧，属 D-A）** |
| --- | --- | --- |
| 长度 | **恒 2 字节**（向量 V7/V7b；`wire_constraints.0xFFF3_min_length = 2`） | 仅要求 `len ≥ 1`（**接受 1 字节**，**未强制 `len == 2`**） |
| CRC | `crc8(idx) == pkt[1]` | **完全不校验 CRC 字节** |
| ATT 错误码 | —（按协议构造即不会触发） | `len < 1` → `0x0D Invalid Attribute Value Length`；`idx > 1` → `0x04 Invalid PDU` |
| 文档一致性 | `docs/PROTOCOL.md` §5 **只写 1 字节、无 CRC 行**，与向量冲突 → **以向量为准** | — |
| 🚫 下游 | — | `ble_get_model_index()` **当前无消费者** → 写入成功但**暂不改变推理行为**（见 §8 的 D-D） |

**Client 断言**（`PROTOCOL_VECTORS.md` §5）：V7 / V7b / **P7（`idx > 1` 必须被拒绝）**。

### 6.6 `0xFFF4` 批量数据同步（Read / Write）

**Read（APP 取回离线事件）**：

| 项 | 规则 |
| --- | --- |
| 返回结构 | **N × 11 字节**，每条结构同 §6.4（自带 CRC）；`N = len / 11` |
| N 上限 | **20**（`BLE_MAX_BATCH_EVENTS`）；实际还受 MTU 截断影响 |
| 语义 | **只预览、不删除**：APP 未 ACK 前事件仍在缓存中，可重读 |
| 空缓存 | 返回 **0 字节** → **APP 不得写 ACK** |
| 整批 CRC | **不追加**（`O-1` 已裁决） |
| 顺序 | **最旧优先** |

**Write（APP 确认，即 ACK）**：

| 项 | 规则 |
| --- | --- |
| 载荷 | **1 字节 `0x01`**（内容**不参与判定**，`wire_constraints` 允许任意 ≥1 字节写入） |
| Server 行为 | 按**上一次 Read 实际交付的条数**（`s_batch_pending`）**精确 pop**；随后清零 pending |
| 无 pending 时 | 记日志忽略（`batch ack without pending read (ignored)`） |
| 单批装不下 | 剩余事件留待下一轮 Read（因此 APP 需要**循环 Read→解析→落库→ACK** 直到返回 0 字节） |
| 缓存容量 | PSRAM 循环缓冲 **100 条**（`KMaxOfflineEvents`），满则覆盖最旧 |

**Client 解析断言**（`PROTOCOL_VECTORS.md` §6）：

1. `11 <= len <= 220` 且 `len % 11 == 0`，否则**整批拒绝并记日志**；
2. **逐条**校验 CRC，`valid=true` 才落库；**任一条坏包只丢弃该条，不得整批丢弃**；
3. 落库完成后向 `0xFFF4` 写 1 字节 `0x01` 作 ACK；
4. Read 返回 0 字节 → **不写 ACK**；
5. MTU 截断时 `len` 仍为 11 的整数倍（可能 < 220），**必须按实际 `len` 处理，不得假定 20 条**。

正向向量：V8a（3 条）、V8b（满批 20 条）。

### 6.7 GATT 错误处理与重试（跨接口，Client 必须实现）

| 场景 | 识别方式 | 要求 |
| --- | --- | --- |
| `status 133`（`GATT_ERROR`） | 读 / 写 / CCCD 写回调返回 133 | 典型于**刚连上立刻操作**；**指数退避后重连再重试**，禁止紧密循环 |
| MTU 协商失败 | `requestMtu` 回调非 0 | 降级到默认 23 → `0xFFF4` 单读载荷仅 20 字节（**1 条事件**）；必须按实际 `len` 处理，**禁止假定 20 条** |
| CCCD 写失败 | `writeDescriptor(0x2902)` 回调非 0 | 串行重试（间隔 ≥ 50 ms），**三个 CCCD 全部成功**才视为订阅完成 |
| 写超时 | `WRITE_TYPE_DEFAULT` 无回调（建议 5 s 超时） | 超时后**不得盲目重写** `0xFFF1` / `0xFFF3`（写入可能已生效）；应重连后重发或用只读手段确认 |
| 断线重连 | `onConnectionStateChange` 断开 | 指数退避（如 1 s / 2 s / 4 s …，上限 30 s）；手表断开后**立即重播广播**（§3.4），无需等广播超时 |
| 读 / 通知重复 | 同一事件重复到达 | `0xFFF2` 按 `type + timestamp_ms + confidence` 去重；`0xFFF4` **ACK 失败或 App 崩溃后重读会拿到同一批**（Read 只预览不删除），**落库必须按唯一键去重** |
| 写类型 | 所有 Write 调用 | 恒用 `WRITE_TYPE_DEFAULT`（§3.3）；`NO_RESPONSE` 会被服务器拒绝 |

---

## 7. 时间戳语义与降级（`open_items.O-2`）

| 要求 | 现状 |
| --- | --- |
| `PROTOCOL.md` §4 规定 `timestamp_ms` = **Unix epoch 毫秒** | 固件 `main.c` 实际用 `esp_timer_get_time() / 1000` = **开机以来毫秒** |

**对 Client 的强制要求**：

1. **禁止**直接把该字段当 epoch 展示 → 否则 APP 会显示 **1970 年**；
2. 解析时若发现值明显小于合理 epoch 下界，应判定为「**未校准**」并显示占位文案，而非 1970 时间；
3. 时间校准依赖计划中的 `0xFFF5`（见 §9 的 D1 / D2），**升版前不得自行实现**；
4. 该问题为 **Phase 2 阻塞项**，最终形态：Server 维护 `epoch_ms = synced_epoch + (esp_timer - synced_at)`，
   未同步前以 flag 标记。

---

## 8. 已知差异与开放项登记（联调前必须确认）

### 8.1 协议真源中已登记的开放项 → Client 影响

| ID | 标题 | 对 Client 的影响 | Client 要求动作 | 归属 |
| --- | --- | --- | --- | --- |
| `O-1` | 🚫 **仍开放**：`PROTOCOL.md` §6 **至今仍写**「末尾带 CRC-8」，与向量 §6「**不追加整批 CRC**」**直接冲突**；且对完整载荷 `crc8(msg)` 恒为 `00` | 若照 `PROTOCOL.md` §6 实现整批 CRC，会**恒校验失败或误判** | **以向量为唯一裁决**：不实现整批 CRC，只做逐条 CRC + `len % 11 == 0`；**忽略 `PROTOCOL.md` §6 的「末尾带 CRC-8」字样** | **Pulse 待办（未关闭）**：走 §12 六步流程修订 `PROTOCOL.md` §6、升版、重算 SHA-256、复制到 Health 后关闭 |
| `O-2` | 时间戳为开机毫秒而非 epoch | 时间显示错误（1970 年） | 按 §7 降级为「未校准」；不要写死 epoch 假设 | 两端 |
| `O-3` | `0x2A37` / `0x2A5F` 载荷偏离 BLE 标准结构 | 通用 BLE App 无法正确解析本设备 | **不要用系统标准 HR / PLX 解析器**，用本文 §6.1/§6.2 自定义解析；**禁止单端更改** | Pulse（记录即可，不阻塞） |
| `O-4` | 广播未设 `0xFFF0` 服务 UUID，**且不保证含设备名** → **设备发现失败** | `ScanFilter.setServiceUuid` 硬件过滤扫不到；名称过滤同样可能失效 | 见 §3.2：Server 修复前**不设任何 ScanFilter**、扫描全部设备，**连接后以服务发现校验 `0x180D` 与 `0xFFF0`**（P0） | 两端（Server 先改） |
| `O-5` | `anomaly_events` 缺 `userId`，与多用户模型不一致 | 手表事件无法按用户隔离 | 第三批评估：`MIGRATION_3_4` 补 `userId`（DEFAULT NULL），**不阻塞当前接线** | Health |
| `O-6` | Health 仓 `.gitignore` 忽略全部 `*.md` 与 `*.json` | 治理文档不入 git，换机即丢，**契约镜像漂移无法被检测** | 精准放行 `!docs/*.md` 与 `!protocol-pin.json`（契约文件必须可审计） | Health（需仓库所有者确认） |

### 8.2 本轮交接核查新发现（✅ 已登记进 `protocol-pin.json.open_items`，⏳ 解决仍属所有者）

> 以下 4 项由本交接文档编写时对照「真源 vs 固件源码」核查得出。
> 按 AGENTS.md §9.2：**以 `docs/PROTOCOL_VECTORS.md` 为唯一裁决依据**。
> **登记状态**：✅ D-A / D-B / D-C / D-D 已于 2026-09-24 登记进 `protocol-pin.json.open_items`（仅元数据新增，未动 `lock_files` / `protocol_version` / `pinned_at` / `status`，未重算哈希、未升版；双仓已 byte-identical）。
> ⏳ **解决**（下表"待办动作"列）仍属仓库所有者待办——收紧文档/固件会改变线格式或固件行为，须走 §12 六步流程升版后才算关闭。

**待所有者解决 · 每项关闭前造成的具体阻塞**（✅ 登记已于 2026-09-24 完成；⏳ 下列"待办动作"仍未执行）：

| 编号 | 待办动作（⏳ 未执行） | 关闭前造成的具体阻塞 |
| --- | --- | --- |
| **D-A** | `PROTOCOL.md` §5 的 `0xFFF3` 长度收紧为 2 字节；固件补 CRC 校验与 `len < 2` 拒绝 | 固件校验无法收紧（现行只校 `len ≥ 1`、`idx ≤ 1`）；Client 侧**验收断言无法据固件行为写定**——1 字节写入到底算成功还是失败，无判据 |
| **D-B** | `PROTOCOL.md` §3 的 `0xFFF1` 保留字段收紧为恒 8 字节 | `PROTOCOL.md` §3 无法收紧，文档与向量长期两说；固件 `len > 32` 的过松分支无判据可依 |
| **D-C** | 固件接线 `ble_notify_spo2()`（当前已定义、0 处调用） | **血氧端到端验收不可做**——验收必须以「收不到血氧」为前提，否则任何血氧断言都会误判 |
| **D-D** | 接线 `ble_get_user_config()` / `ble_get_model_index()` 的下游消费 | **下行配置验收判据缺失**——写入返回成功但手表行为不变，只能以日志为准，无法验证配置真正生效 |

> ✅ **登记已完成**（2026-09-24）：D-A…D-D 已写入 `protocol-pin.json.open_items`（仅元数据，未动哈希/版本/`status`）。
> ⏳ **解决**（上表"待办动作"）仍需仓库所有者走 §12 六步流程——收紧文档/固件会改变线格式或固件行为，须升版后才算关闭。

| 编号 | 差异事实 | 裁决（以向量为准） | Client 行动 | 建议归属 |
| --- | --- | --- | --- | --- |
| **D-A** | `0xFFF3` 三处不一致：`PROTOCOL.md` §5 只写 1 字节、无 CRC 行；向量 V7/V7b 与 `wire_constraints.0xFFF3_min_length` 要求 **2 字节（idx + crc8）**；固件 `on_chr_fff3_write` 只校 `len≥1`、`idx≤1`，**未校验 CRC、未强制 2 字节** | Client **必须发 2 字节**：`idx` + `crc8(idx)` | 按 §6.5 构造；固件待收紧（补 CRC 校验与 `len<2` 拒绝） | Pulse（文档 + 固件） |
| **D-B** | `0xFFF1` 保留字段长度松：`PROTOCOL.md` §3 写 `6 n uint8[] 保留`（变长）；向量 V6 明确 `reserved[1]=0x00` 且**总长恒 8**；固件校 `len<8` 或 `len>32` 拒绝 | Client **恒发 8 字节** | 构造器固定返回 8 字节，不发变长 | Pulse（文档收紧） |
| **D-C** | `0x2A5F` 血氧「有定义、无发送」：`ble_notify_spo2()` 在 `gatt_server.c` 中**已定义但全固件 0 处调用**（`main.c` 只调 `ble_notify_heart_rate` / `ble_notify_anomaly`） | 接口有效但**当前无数据** | UI 先标「暂无数据」；**验收不以收到血氧为通过条件**；待 Server 接线 | Pulse（固件接线） |
| **D-D** | `0xFFF1` / `0xFFF3` 写入暂无下游消费：`ble_get_user_config()` 与 `ble_get_model_index()` **已定义但全固件无人调用** → 写入后手表行为不变 | Client 正常写、正常读回（按向量） | 验收以「写入返回成功 + Server 日志打印」为准，**不要求手表行为变化** | Pulse（后续接线） |

---

## 9. 待决决策与禁止实现项

### 9.1 🚫 升版前禁止实现

| 项 | 说明 |
| --- | --- |
| `0xFFF5`（时间同步） | 必须在 **D1 决策通过 + 协议升版 + 新增向量 + 更新 `protocol-pin.json`** 之后才可实现；**在此之前任何一端都不得实现** |
| `0xFFF6`（体征上传） | 必须在 **D4 决策通过 + 协议升版** 之后才可实现；同上 |
| 向量覆盖范围 | 当前向量**仅覆盖** `PROTOCOL.md` §1–§6 现行线格式（`0x2A37` / `0x2A5F` / `0xFFF1`–`0xFFF4`） |
| `type=2` / `type=3` 的业务语义 | 展示样式、告警分级、落库字段映射**不属于线格式**，由 Client 语义层自行定义 |

### 9.2 待决决策对 Client 的影响

| 决策 | 主题 | 对 Client 的影响 |
| --- | --- | --- |
| D1 | 是否新增 `0xFFF5` 时间同步（Write 授时 + Read 回读校验） | 决定 §7 降级是临时方案还是永久方案 |
| D2 | 未同步期 `timestamp_ms` 降级策略 | 推荐 reserved 标志位 + APP 显示「未校准」 |
| D3 | 传输层准入（最小绑定 LE Just Works + bonding + `createBond()`） | 决定是否需要处理配对弹窗 / 被拒连；**未配置 Security Manager 前任何人可连上读走数据** |
| D4 | 体征时序落库范围（收敛为 `vitals_samples` + HR/SpO2，不含体温） | 决定 Client 落库表结构；排除体温（固件无体温通道） |
| D5 | 契约测试载体分档（第一批内联 `feature:watch/src/test`，第二批抽 `core:pulse-protocol`） | 决定 §10 的测试落点与命令 |
| D6 | 移除 Room `fallbackToDestructiveMigration` | 改用显式 Migration，避免升级丢数据 |

> 各决策的**推荐结论、范围与人日估算**以 `protocol-pin.json` → `pending_decisions` 为唯一数值真源，
> 本文不复制其中的版本与估算数值。

---

## 10. 契约测试落地（两侧必跑）

| 仓库 | 角色 | 实现位置 | 命令 |
| --- | --- | --- | --- |
| TianshangPulse | Server | `tests/host/test_protocol.c`（`tests/host/stubs/` 已备 FreeRTOS / esp_log 桩） | 见 `PLAN_WATCH_INTEGRATION.md` Phase 4.6 任务卡 |
| TianshangHealth | Client | 第一批：`feature:watch` 内联测试；第二批：`core:pulse-protocol` 模块 `ProtocolCodecTest.kt`（纯 JVM，无 Android 依赖）（分档由 D5 决定） | 第二批：`./gradlew :core:pulse-protocol:test` |

> ⚠️ **前置条件（Health 侧）—— 已按第一批交付回填**：本文档初版据当时 README 判断 Health 侧**既无** `feature/watch` **也无** `core/pulse-protocol`，故结论「测试落点与命令不可执行」对**两档同时成立**。现状必须拆成两态：
>
> - **第一批（内联）—— 已落地、命令可执行**：`feature/watch` **已存在**（`settings.gradle` 已注册、`app` 已依赖、导航与 Dashboard 入口已接入）；Health README 已补 Watch 特性与 BLE 权限表，并显式声明**不需要 `INTERNET`**。契约测试已内联为 `feature/watch/src/test/.../ble/ProtocolCodecTest.kt`，`./gradlew :feature:watch:test` 实跑通过（结果见 `HANDOFF_REPORT_HEALTH_SIDE.md` §7）。
> - **第二批（抽取）—— 仍未就绪、命令不可执行**：`core/pulse-protocol` **确实不存在**（按 D5 分档推迟，本轮有意不建），故上表「第二批：`./gradlew :core:pulse-protocol:test`」一行**依然不可执行**，须待 D5 拍板 + Pulse 侧升版后另行落地。
>
> 另：初版此处列出的模块清单还漏了 `core/period-api`，一并补齐。


**要求**：

1. 两侧对 **V1–V10f 全组**断言（共 24 条），同一 ID 的期望字节**完全相同**；
2. 期望字节**只以 `docs/PROTOCOL_VECTORS.md` 为准**（本文不复制，避免双份真相源）；
3. **代码与向量冲突 → 以向量为准并立刻修代码**，不得反向改向量；
4. 反例（P6a / P6b / P7 / V9 / V9b / V10c / V10f）**必须同样断言**，用于防止「解析过松」；
5. 本向量表状态为 **FROZEN**；任何变更必须先走 §12 的六步流程。

---

## 11. Client 实施任务清单与验收标准

| 优先级 | 任务 | 验收标准 |
| --- | --- | --- |
| **P0** | 放宽扫描过滤（`O-4`） | 能扫到 `TianshangPulse`；连接后服务发现含 `0x180D` 与 `0xFFF0` 两个服务 |
| **P0** | MTU 协商 | `requestMtu(512)` 成功；实测 `0xFFF4` 单读返回长度可 > 20 字节 |
| **P0** | 写类型约束 | `0xFFF1` / `0xFFF3` / `0xFFF4` 全部使用 `WRITE_TYPE_DEFAULT`，无 NO_RESPONSE 调用点 |
| **P0** | CCCD 串行订阅 | 订阅 `0x2A37` / `0x2A5F` / `0xFFF2` 三个 CCCD，逐个串行写入并等待回调 |
| **P0** | Android 权限与扫描前置 | 按 §3.5：API 31+ 授予 `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT`，API ≤ 30 授予 `ACCESS_FINE_LOCATION`；蓝牙与位置服务未开启时有引导提示 |
| **P0** | 创建 `feature:watch` 模块 | Health README 架构新增该模块并可编译，第一批契约测试在此内联落地（§10 前置条件）；第二批再抽 `core:pulse-protocol` |
| **P1** | `0x2A37` / `0x2A5F` 解析 | 长度不足返回 `null` 且不抛异常；断言 V10–V10f |
| **P1** | `0xFFF1` 构造器 | 长度恒 8、`crc8(pkt[0..6]) == pkt[7]`；断言 V6/V6b/V6c/V6d + 反例 P6a/P6b |
| **P1** | `0xFFF2` 解析器 | 11 字节校验 + 字段拆解；断言 V2/V3/V4/V5/V5b + 反例 V9/V9b；`type` 映射四类事件 |
| **P1** | `0xFFF3` 构造器 | 恒 2 字节 `idx + crc8(idx)`（§8 的 D-A）；断言 V7/V7b/P7 |
| **P1** | `0xFFF4` 同步状态机 | 循环 `Read → 长度校验 → 逐条 CRC → 落库 → ACK(0x01) → 循环到返回 0 字节`；空读不写 ACK；坏包只丢该条；断言 V8a/V8b |
| **P1** | 契约测试 | 24 条向量全绿（位置见 §10） |
| **P1** | GATT 错误处理与重试 | 按 §6.7：133 退避重连、MTU 降级按实际 `len`、CCCD 串行重试、写超时策略、指数退避重连、读/通知重复去重 |
| **P2** | 时间戳降级（`O-2`） | 未校准时间不显示为 1970 年，显示「未校准」占位 |
| **P2** | 事件落库 | `type` / `timestamp` / `confidence` 落库；`userId` 按 `O-5` 第三批处理 |
| **P2** | 断连重连 | 断开后自动重连（Server 立即重播广播）；连接时手表进入 ACTIVE、断开进入 LIGHT_SLEEP 可观测 |
| **P2** | README 隐私与权限声明 | Health README 增列 BLE 所需蓝牙权限（§3.5），并显式声明**不需要 `INTERNET`**（全程离线） |

> 范围与人日估算（第一批 / 第二批）以 `protocol-pin.json` → `pending_decisions.scope` 为真源。

---

## 12. 变更与同步义务

1. **协议变更六步**（AGENTS.md §9.2，顺序不可颠倒；本仓库执行第 1–4 步）：

   ```
   Pulse 改 PROTOCOL.md → Pulse 更新 PROTOCOL_VECTORS.md → Pulse 重算 SHA-256 写 protocol-pin.json
      → 复制三个文件到 Health（byte 级） → 双端实现 + 跑契约测试 → 双端更新文档
   ```

2. **版本规则**：兼容新增（如新增特征值）→ **MINOR**；改字段语义/长度/字节序 → **MAJOR**；裁决与措辞 → **PATCH**；
3. 新增向量**不得**修改既有向量的期望值；
4. 发现协议矛盾（文档 vs 代码）时：以 `docs/PROTOCOL_VECTORS.md` 为唯一裁决依据，
   **先登记 `protocol-pin.json` → `open_items`**（本文 §8.2 的 D-A / D-B / D-C / D-D 即属此类，✅ 已登记、⏳ 解决待办），再暂停实现并上报；
5. **本文件的同步义务**：本文件不是四同步点之一，不参与 SHA-256 锁定；但**协议任何变更都必须复核本文件**，
   保持接口描述与真源一致（发现不一致以真源为准并修订本文件）；
6. 本文件与 `PLAN_WATCH_INTEGRATION.md` 一样，应**同名复制到 TianshangHealth 仓库**；
   注意 `O-6`（Health 侧 `.gitignore` 忽略全部 `*.md`）需先放行，否则该文件进不了 git。
   **本轮已执行**：Health 根目录已建同名副本。但因 Health `.gitignore` 仍忽略 `*.md`（仅对报告与契约文件加了白名单），
   **该副本不入 git**——交接文档的「双仓 byte-identical」因此**只能靠内容/哈希比对校验，不能靠 `git status`**。
   附带效果：简报的必读项 R5 原先指向本文件，而 Health 仓当时并不存在它，R5 只能跨仓解析；副本就位后 R5 可直接在本仓读到。

---

## 13. 边界声明

1. **未在实机验证**：本文件基于 `docs/PROTOCOL.md`、`docs/PROTOCOL_VECTORS.md`、`protocol-pin.json`
   与 `firmware/main/ble/gatt_server.c` 源码交叉核对生成，**未经真实硬件联调**。
   无实机时以下三项**无法确认**：广播载荷实际内容、配对是否拦住未绑定设备、MTU 实际协商值；
2. **§8.2 的 D-A / D-B / D-C / D-D 为本轮新发现**，✅ 已于 2026-09-24 登记进 `protocol-pin.json.open_items`
   （仅元数据新增，未动 `lock_files` / `protocol_version` / `pinned_at` / `status`，未重算哈希、未升版）；
   ⏳ 各项「待办动作」（收紧 `PROTOCOL.md` §3/§5、固件补 CRC 校验与 `ble_notify_spo2` 接线等）仍属所有者，须走 §12 六步流程升版后方可关闭；
3. 本文**不含协议版本号与 SHA-256**；需要这些数值时只读 `protocol-pin.json`；
4. 本文**未修改**协议真源 `docs/PROTOCOL.md` 与 `docs/PROTOCOL_VECTORS.md`（四项 `lock_files` 哈希未变）；
   `protocol-pin.json` 仅在 `open_items` 追加 D-A…D-D 四条元数据（2026-09-24），**未改** `lock_files` / `protocol_version` / `pinned_at` / `status`、**未重算**任何哈希、**未触发**协议变更六步流程（六步流程只针对线格式变更，登记 `open_items` 不改线格式）。
5. **本轮已按 Server 侧核查意见完成问题 1–7 的修订**（明细见文末「修订记录」）；
   「建议修订顺序」第 7 项（把 D-A…D-D 登记进 `protocol-pin.json.open_items`）已于 **2026-09-24 执行**
   （R2 对齐轮，用户授权「登记进 pin」）——仅追加 `open_items` 元数据，未动冻结核（哈希/版本/`status`）。
   ⏳ 各项**解决**（收紧 `PROTOCOL.md` / 固件接线）仍属所有者，须走 §12 六步流程升版后方可关闭；
   关闭前本文档的 Client 验收断言以「向量为准 + 固件当前行为」为基线（见 §8.2 表）；
6. **Health 侧前置状态（已按第一批交付回填）**：`feature:watch` 模块与 §3.5 的 Android BLE 权限**已落地**
   （Health README 已增权限表并显式声明不需要 `INTERNET`）；`core:pulse-protocol` **仍不存在**（按 D5 分档推迟）。
   §11 的**第一批 P0 已完成**，**第二批 P0 仍待 Pulse 侧升版**——
   此前「§11 的 P0 项无法执行完毕」的判断对第一批已失效，对第二批依然成立；
7. **范围声明**：
   - **R1 文档对齐（2026-09-24）**：已对齐三份交接文档（本文、`AGENT_HANDOFF_TIANSHANGHEALTH.md`、
     `HANDOFF_REPORT_HEALTH_SIDE.md`），修掉彼此矛盾与过期陈述（Health 侧模块/权限前置、简报未闭环、
     报告的 D1–D6 授权误归属、测试计数口径、git 可见性陈述）；该轮**未改**协议真源、**未登记** `open_items`、
     **未触碰**固件代码、**未执行**任何 commit。
   - **R2 缺陷登记（2026-09-24，用户授权「登记进 pin」）**：向 `protocol-pin.json.open_items` 追加 D-A…D-D 四条元数据
     （仅元数据，未动 `lock_files` / `protocol_version` / `pinned_at` / `status`，未重算哈希、未升版，双仓已 byte-identical）；
     ⏳ 各项**解决**仍属所有者。本轮**仍未触碰**固件代码、**仍未** commit、**仍未改** `docs/PROTOCOL.md` 与 `docs/PROTOCOL_VECTORS.md`。

---

## 修订记录（响应 Server 侧核查意见）

| 意见 | 章节 | 处理 |
| --- | --- | --- |
| 问题 1 | §8.1 `O-1` | ✅ 纠正为「**仍开放**」：`PROTOCOL.md` §6 至今仍写「末尾带 CRC-8」，与向量直接冲突；Client 以向量为唯一裁决；Server 待走六步流程关闭 |
| 问题 2 | §3.2 / §8.1 `O-4` | ✅ 兜底改为「**不设任何 ScanFilter → 扫描全部 → 连接后服务发现校验**」，并点明设备名不进广播载荷 |
| 问题 3 | §6.3 | ✅ 拆为「协议要求 vs 当前固件行为」三列表；补 `reserved` 不校验、CRC 实际比较 `buf[len-1]` |
| 问题 4 | §6.5 | ✅ 同样拆分；显式化 `len == 2` + `crc8(idx) == pkt[1]` 与 `PROTOCOL.md` §5 的冲突 |
| 问题 5 | §10 / §11 | ✅ 新增 Health 模块前置警告与 P0 建模块任务 |
| 问题 6 | §3.5（新增） | ✅ 新增 Android 权限与扫描前置条件 |
| 问题 7 | §4 / §6.7 / §11（新增） | ✅ 128-bit UUID 表、GATT 错误处理与重试、README 隐私声明任务 |
| 自检 | 全文 | ✅ 修复 2 处 setext 渲染缺陷（`---` 紧跟正文行会被解析为二级标题） |
| **待办** | `protocol-pin.json` | ✅ R2（2026-09-24，用户授权「登记进 pin」）已将 D-A / D-B / D-C / D-D 登记进 `open_items`（仅元数据，未动冻结核）；⏳ 各项**解决**（收紧文档 / 固件接线）仍属所有者，须走 §12 六步流程升版后关闭（见 §8.2 表、§13.5） |
| 本轮对齐（2026-09-24） | §8.2 / §10 / §12.6 / §13 / 页脚 | ✅ §8.2 登记项升级为「待所有者执行 + 具体阻塞」行动清单；回填 Health 侧模块与权限的真实状态（`feature:watch` 已落地、`core:pulse-protocol` 仍不存在、模块清单补 `core/period-api`）；记录 §12.6 镜像已执行及其 git 可见性边界；新增 §13.7 范围声明；页头状态改为「两道独立闸门」 |
| R2 登记级联（2026-09-24） | §8.2 / §12.4 / §13.2 / §13.4 / §13.5 / §13.7 / 修订记录 / 页脚 | ✅ D-A…D-D 登记进 `open_items` 后，全文原先声称「尚未登记 / 未改 pin / 未登记 open_items」的陈述已同步更正为「已登记（仅元数据），解决仍属所有者」；§13.7 拆为 R1 文档对齐 + R2 缺陷登记两段 |

---

*编写：TianshangPulse（Server 侧） · 真源见 §2 · 变更流程见 §12*
*状态：✅ 已按 Health 侧第一批交付回填，并与其他两份交接文档对齐（2026-09-24）· ✅ D-A…D-D 已登记进 `open_items`（R2，2026-09-24），⏳ 解决仍属所有者（升版后关闭） · ⏳ 第二批待 Pulse 侧 `0xFFF5` 升版 · ⏳ D1–D6 待用户拍板 · 全文**仍待实机联调**（硬件在途）*
