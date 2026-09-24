/* 协议纯编解码实现（见 ble/protocol.h）。
 *
 * 本文件由 firmware/main/ble/gatt_server.c 中原本的 static 函数原样抽出，
 * 行为必须与抽出前**完全一致**（P4「行为不变」）；任何线格式变更须走协议升版流程。
 * 只依赖 <string.h>，禁止引入 NimBLE / ESP-IDF 头，否则 host 契约测试无法编译。
 */
#include <string.h>
#include "ble/protocol.h"

uint8_t ble_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* 打包异常事件为 PROTOCOL.md §4 载荷：type + ts(LE) + conf + crc8（小端） */
size_t ble_pack_anomaly(const ble_anomaly_event_t *event, uint8_t *out, size_t cap)
{
    if (!event || !out || cap < BLE_ANOMALY_PKT_LEN) return 0;
    out[0] = (uint8_t)event->type;
    uint64_t ts = event->timestamp_ms;
    for (size_t i = 0; i < 8; i++) {
        out[1 + i] = (uint8_t)(ts >> (8 * i));
    }
    out[9] = event->confidence;
    out[10] = ble_crc8(out, BLE_ANOMALY_DATA_LEN);
    return BLE_ANOMALY_PKT_LEN;
}

/* 0xFFF1 构造：hr[2 LE] + spo2[1] + gender[1] + age[2 LE] + reserved[1]=0 + crc8[1] */
size_t ble_build_user_config(uint16_t hr_bpm, uint8_t spo2_pct, uint8_t gender,
                             uint16_t age, uint8_t *out, size_t cap)
{
    if (!out || cap < BLE_USER_CFG_PKT_LEN) return 0;
    out[0] = (uint8_t)(hr_bpm & 0xFF);
    out[1] = (uint8_t)(hr_bpm >> 8);
    out[2] = spo2_pct;
    out[3] = gender;
    out[4] = (uint8_t)(age & 0xFF);
    out[5] = (uint8_t)(age >> 8);
    out[6] = 0x00;                                   /* reserved（向量 V6 固定为 0x00） */
    out[7] = ble_crc8(out, BLE_USER_CFG_DATA_LEN);
    return BLE_USER_CFG_PKT_LEN;
}

/* 0xFFF1 解析（Server 侧消费）。
 * 忠实复刻固件原行为：接受 len ∈ [8, 32]，CRC 覆盖前 7 字节并与**末字节**比较。
 * 注意（open_items D-B）：向量 V6 要求总长恒 8 且 reserved[1]=0x00，本函数沿用固件
 * 「过松」的 len 上界，收紧须走协议升版流程，不得在此单方面更改。 */
int ble_parse_user_config(const uint8_t *in, size_t len, ble_user_config_t *out)
{
    if (!in || !out) return BLE_PROTO_ERR_LEN;
    if (len < BLE_USER_CFG_PKT_LEN || len > BLE_USER_CFG_MAX_LEN) return BLE_PROTO_ERR_LEN;
    if (ble_crc8(in, BLE_USER_CFG_DATA_LEN) != in[len - 1]) return BLE_PROTO_ERR_CRC;
    out->hr_threshold_bpm   = (uint16_t)(in[0] | (in[1] << 8));
    out->spo2_threshold_pct = in[2];
    out->gender             = in[3];
    out->age                = (uint16_t)(in[4] | (in[5] << 8));
    return BLE_PROTO_OK;
}

/* 0xFFF3 构造：idx[1] + crc8(idx)[1] */
size_t ble_build_model_switch(uint8_t idx, uint8_t *out, size_t cap)
{
    if (!out || cap < BLE_MODEL_SWITCH_LEN || idx > 1) return 0;
    out[0] = idx;
    out[1] = ble_crc8(out, 1);
    return BLE_MODEL_SWITCH_LEN;
}

/* 0xFFF3 解析（Server 侧消费）。
 * 忠实复刻固件原行为：只校验 idx <= 1，**未校验 CRC、未强制 2 字节**。
 * 注意（open_items D-A）：向量 V7/V7b 要求 2 字节 idx + crc8，收紧（补 CRC 校验与
 * len<2 拒绝）须走协议升版流程，不得在此单方面更改。 */
int ble_parse_model_switch(const uint8_t *in, size_t len, uint8_t *out_idx)
{
    if (!in || !out_idx) return BLE_PROTO_ERR_LEN;
    if (len < 1) return BLE_PROTO_ERR_LEN;
    if (in[0] > 1) return BLE_PROTO_ERR_RANGE;
    *out_idx = in[0];
    return BLE_PROTO_OK;
}

/* 0x2A37 心率载荷：uint16 LE，无 flags 字节（O-3 有意偏离标准） */
size_t ble_encode_hr(uint16_t bpm, uint8_t *out, size_t cap)
{
    if (!out || cap < BLE_HR_PKT_LEN) return 0;
    out[0] = (uint8_t)(bpm & 0xFF);
    out[1] = (uint8_t)(bpm >> 8);
    return BLE_HR_PKT_LEN;
}

/* 0x2A5F 血氧载荷：uint8（O-3 有意偏离标准 PLX 结构） */
size_t ble_encode_spo2(uint8_t pct, uint8_t *out, size_t cap)
{
    if (!out || cap < BLE_SPO2_PKT_LEN) return 0;
    out[0] = pct;
    return BLE_SPO2_PKT_LEN;
}
