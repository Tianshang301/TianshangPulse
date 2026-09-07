# TianshangPulse GATT 协议

> **Source of Truth**：本文件是 BLE GATT 协议的唯一定义。手表端（Server）定义协议，APP 端（Client）遵循协议。
> 任何修改必须走「协议变更流程」，见 AGENTS.md §3.2。

## 1. 字节序与校验

- 数据包格式：**Little Endian（小端序）**
- 校验：**CRC-8**（多项式 `0x07`，初值 `0x00`），附加于有效载荷末尾

## 2. GATT 特征值表

| UUID | 属性 | 方向 | 说明 | 载荷格式 |
|------|------|------|------|----------|
| `0x2A37` | Notify | Watch→APP | 实时心率 | `uint16_t bpm` |
| `0x2A5F` | Notify | Watch→APP | 实时血氧 | `uint8_t pct` |
| `0xFFF1` | Write | APP→Watch | 用户配置（阈值/性别/年龄） | 见 §3 |
| `0xFFF2` | Notify | Watch→APP | 异常事件 | 见 §4 |
| `0xFFF3` | Write | APP→Watch | 模型 A/B 切换 | 见 §5 |
| `0xFFF4` | Read/Write | 双向 | 批量数据同步（离线缓存 PPG） | 见 §6 |

> 父服务：`0x2A37`/`0x2A5F` 注册于标准心率服务 **`0x180D`**；`0xFFF1`–`0xFFF4` 注册于自定义服务 **`0xFFF0`**。

## 3. 用户配置 `0xFFF1`（Write）

```
偏移  长度  类型      字段
0     2     uint16   心跳异常阈值 (BPM)
2     1     uint8    血氧异常阈值 (%)
3     1     uint8    性别 (0=女 1=男)
4     2     uint16   年龄
6     n     uint8[]  保留
```
CRC-8 附加于末尾。

## 4. 异常事件 `0xFFF2`（Notify）

```
偏移  长度  类型       字段
0     1     uint8     事件类型 (0=HR异常 1=SpO2异常 2=模型切换 3=AF异常)
1     8     uint64    时间戳 (ms, Unix)
9     1     uint8     置信度 (0-100)
10    n     uint8[]   保留
```
CRC-8 附加于末尾。

## 5. 模型切换 `0xFFF3`（Write）

```
0     1     uint8  模型索引 (0=A, 1=B)
```
0x00 表示回退到默认模型。

## 6. 批量数据同步 `0xFFF4`（Read/Write）

- Read：取回一批离线缓存事件（≤ 20 条，小端序，每条见 §4，末尾带 CRC-8）
- Write：APP 确认已收到，Watch 侧清除对应缓存

## 7. 离线缓存策略

- 无网/未连接时，异常事件写入 PSRAM 循环缓冲区（`KMaxOfflineEvents = 100` 条）
- 恢复连接后自动批量上报，APP 确认后清除
- 缓冲区满时覆盖最旧记录
