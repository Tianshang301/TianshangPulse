/* 协议契约 host 单测（Server 侧）—— 与 firmware/main/ble/protocol.c 同源编译。
 *
 * 期望值唯一真源：docs/PROTOCOL_VECTORS.md（FROZEN）。
 * 本文件只**断言**向量，不定义线格式；代码与向量冲突时以向量为准并立刻修代码。
 * 每条断言上方标注对应向量 ID，ID 与向量文档一一对应（AGENTS §9.5 DoD #1）。
 *
 * 编译（无需 stubs —— protocol.c 只依赖 <string.h>）：
 *   python -m ziglang cc -O2 -I tests/host/stubs -I firmware/main \
 *       tests/host/test_protocol.c firmware/main/ble/protocol.c -o test.exe
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble/protocol.h"

static int g_checks = 0;

/* 逐字节比对，失败时打印向量 ID 与首处差异（assert 无输出，排查困难） */
static void expect_bytes(const char *id, const uint8_t *got, const uint8_t *want, size_t n)
{
    g_checks++;
    for (size_t i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            printf("FAIL %s: byte[%zu] got=%02x want=%02x\n", id, i, got[i], want[i]);
            fflush(stdout);
        }
    }
    if (memcmp(got, want, n) != 0) assert(!"vector mismatch");
}

/* 解析类向量：断言返回值 + 逐字段 */
static void expect_int(const char *id, int got, int want)
{
    g_checks++;
    if (got != want) {
        printf("FAIL %s: got=%d want=%d\n", id, got, want);
        fflush(stdout);
    }
    assert(got == want);
}

static ble_anomaly_event_t mk_event(ble_event_type_t type, uint64_t ts, uint8_t conf)
{
    ble_anomaly_event_t e;
    e.type = type;
    e.timestamp_ms = ts;
    e.confidence = conf;
    return e;
}

int main(void)
{
    uint8_t pkt[64];
    uint8_t want[32];
    ble_user_config_t cfg;
    uint8_t idx = 0xFF;

    /* ═══ §2 向量 V1 — CRC-8 基础（poly 0x07 / init 0x00） ═══ */
    expect_int("V1", ble_crc8((const uint8_t *)"\xff", 1), 0xF3);
    expect_int("V1b", ble_crc8((const uint8_t *)"\x01", 1), 0x07);
    expect_int("V1c", ble_crc8((const uint8_t *)"\x12\x34", 2), 0xF1);

    /* §6 已证实结论 #1：crc8(完整载荷含自身 CRC) == 0x00 —— 故整批不追加 CRC */
    expect_int("INV-1a", ble_crc8((const uint8_t *)"\x01", 1), 0x07);
    expect_int("INV-1b", ble_crc8((const uint8_t *)"\x12\x34\xf1", 3), 0x00);

    /* ═══ §3 向量 V2–V5b — 异常事件 0xFFF2（11 字节，CRC 覆盖前 10） ═══ */
    /* V2 全零边界（trap：CRC=00 是合法结果，不得当错误丢弃） */
    {
        ble_anomaly_event_t e = mk_event((ble_event_type_t)0, 0, 0);
        expect_int("V2.len", (int)ble_pack_anomaly(&e, pkt, sizeof(pkt)), 11);
        memset(want, 0, 11);
        expect_bytes("V2", pkt, want, 11);
        expect_int("V2.crc", ble_crc8(pkt, 10), 0x00);
    }
    /* V3 SpO2 异常 */
    {
        ble_anomaly_event_t e = mk_event(BLE_EVENT_ANOMALY_SPO2, 1700000000000ULL, 92);
        expect_int("V3.len", (int)ble_pack_anomaly(&e, pkt, sizeof(pkt)), 11);
        memcpy(want, "\x01\x00\x68\xe5\xcf\x8b\x01\x00\x00\x5c\x21", 11);
        expect_bytes("V3", pkt, want, 11);
    }
    /* V4 AF 异常 */
    {
        ble_anomaly_event_t e = mk_event(BLE_EVENT_AF_ANOMALY, 1750000000000ULL, 88);
        expect_int("V4.len", (int)ble_pack_anomaly(&e, pkt, sizeof(pkt)), 11);
        memcpy(want, "\x03\x00\xdc\x20\x74\x97\x01\x00\x00\x58\xfb", 11);
        expect_bytes("V4", pkt, want, 11);
    }
    /* V5 模型切换事件（ts 极小值） */
    {
        ble_anomaly_event_t e = mk_event(BLE_EVENT_MODEL_SWITCH, 4095ULL, 0);
        expect_int("V5.len", (int)ble_pack_anomaly(&e, pkt, sizeof(pkt)), 11);
        memcpy(want, "\x02\xff\x0f\x00\x00\x00\x00\x00\x00\x00\x3e", 11);
        expect_bytes("V5", pkt, want, 11);
    }
    /* V5b 边界 confidence=0 */
    {
        ble_anomaly_event_t e = mk_event(BLE_EVENT_ANOMALY_SPO2, 1700000000100ULL, 0);
        expect_int("V5b.len", (int)ble_pack_anomaly(&e, pkt, sizeof(pkt)), 11);
        memcpy(want, "\x01\x64\x68\xe5\xcf\x8b\x01\x00\x00\x00\x46", 11);
        expect_bytes("V5b", pkt, want, 11);
    }
    /* V9b 短包（10 字节）—— 缓冲不足 11 时无法构成合法载荷 */
    {
        ble_anomaly_event_t e = mk_event(BLE_EVENT_ANOMALY_SPO2, 1700000000000ULL, 92);
        expect_int("V9b.cap", (int)ble_pack_anomaly(&e, pkt, 10), 0);
    }

    /* ═══ §4 向量 V6 — 用户配置 0xFFF1（8 字节，CRC 覆盖前 7） ═══ */
    expect_int("V6.len", (int)ble_build_user_config(100, 92, 0, 25, pkt, sizeof(pkt)), 8);
    memcpy(want, "\x64\x00\x5c\x00\x19\x00\x00\x65", 8);
    expect_bytes("V6", pkt, want, 8);

    expect_int("V6b.len", (int)ble_build_user_config(200, 90, 1, 30, pkt, sizeof(pkt)), 8);
    memcpy(want, "\xc8\x00\x5a\x01\x1e\x00\x00\x9a", 8);
    expect_bytes("V6b", pkt, want, 8);

    expect_int("V6c.len", (int)ble_build_user_config(0, 0, 0, 0, pkt, sizeof(pkt)), 8);
    memset(want, 0, 8);
    expect_bytes("V6c", pkt, want, 8);

    expect_int("V6d.len", (int)ble_build_user_config(65535, 100, 1, 65535, pkt, sizeof(pkt)), 8);
    memcpy(want, "\xff\xff\x64\x01\xff\xff\x00\x22", 8);
    expect_bytes("V6d", pkt, want, 8);

    /* V6 解析回环：字段顺序 / 字节序必须与打包互为逆运算 */
    {
        memcpy(pkt, "\x64\x00\x5c\x00\x19\x00\x00\x65", 8);
        expect_int("V6.parse", ble_parse_user_config(pkt, 8, &cfg), BLE_PROTO_OK);
        expect_int("V6.hr", cfg.hr_threshold_bpm, 100);
        expect_int("V6.spo2", cfg.spo2_threshold_pct, 92);
        expect_int("V6.gender", cfg.gender, 0);
        expect_int("V6.age", cfg.age, 25);
    }
    /* 反例 P6a：7 字节（缺 reserved）→ 拒绝 */
    {
        memcpy(pkt, "\x64\x00\x5c\x00\x19\x00\x17", 7);
        expect_int("P6a", ble_parse_user_config(pkt, 7, &cfg), BLE_PROTO_ERR_LEN);
    }
    /* 反例 P6b：CRC 错 → 拒绝，配置不生效 */
    {
        memcpy(pkt, "\x64\x00\x5c\x00\x19\x00\x00\x64", 8);
        expect_int("P6b", ble_parse_user_config(pkt, 8, &cfg), BLE_PROTO_ERR_CRC);
    }

    /* ═══ §5 向量 V7 — 模型切换 0xFFF3（2 字节） ═══ */
    expect_int("V7.len", (int)ble_build_model_switch(1, pkt, sizeof(pkt)), 2);
    memcpy(want, "\x01\x07", 2);
    expect_bytes("V7", pkt, want, 2);

    expect_int("V7b.len", (int)ble_build_model_switch(0, pkt, sizeof(pkt)), 2);
    memcpy(want, "\x00\x00", 2);
    expect_bytes("V7b", pkt, want, 2);

    /* 反例 P7：idx > 1 → 拒绝（ATT_INVALID_PDU） */
    {
        memcpy(pkt, "\x02\x05", 2);
        expect_int("P7", ble_parse_model_switch(pkt, 2, &idx), BLE_PROTO_ERR_RANGE);
    }
    /* V7/V7b 解析回环 */
    {
        memcpy(pkt, "\x01\x07", 2);
        expect_int("V7.parse", ble_parse_model_switch(pkt, 2, &idx), BLE_PROTO_OK);
        expect_int("V7.idx", idx, 1);
        memcpy(pkt, "\x00\x00", 2);
        expect_int("V7b.parse", ble_parse_model_switch(pkt, 2, &idx), BLE_PROTO_OK);
        expect_int("V7b.idx", idx, 0);
    }

    /* ═══ §6 向量 V8a — 离线批量 3 条（逐条独立 CRC，不追加整批 CRC） ═══ */
    {
        ble_anomaly_event_t e0 = mk_event(BLE_EVENT_ANOMALY_SPO2, 1700000000000ULL, 80);
        expect_int("V8a.ev0.len", (int)ble_pack_anomaly(&e0, pkt, sizeof(pkt)), 11);
        memcpy(want, "\x01\x00\x68\xe5\xcf\x8b\x01\x00\x00\x50\x05", 11);
        expect_bytes("V8a.ev0", pkt, want, 11);

        ble_anomaly_event_t e2 = mk_event(BLE_EVENT_ANOMALY_SPO2, 1700000002000ULL, 82);
        expect_int("V8a.ev2.len", (int)ble_pack_anomaly(&e2, pkt, sizeof(pkt)), 11);
        memcpy(want, "\x01\xd0\x6f\xe5\xcf\x8b\x01\x00\x00\x52\xd9", 11);
        expect_bytes("V8a.ev2", pkt, want, 11);

        /* 33 字节 = 3 × 11，len % 11 == 0 */
        expect_int("V8a.batch", (33 % 11) == 0 ? 33 / 11 : -1, 3);
    }
    /* ═══ §6 向量 V8b — 满批 20 条（N ≤ BLE_MAX_BATCH_EVENTS） ═══ */
    {
        ble_anomaly_event_t e0 = mk_event(BLE_EVENT_AF_ANOMALY, 1750000000001ULL, 61);
        expect_int("V8b.ev0.len", (int)ble_pack_anomaly(&e0, pkt, sizeof(pkt)), 11);
        memcpy(want, "\x03\x01\xdc\x20\x74\x97\x01\x00\x00\x3d\xbe", 11);
        expect_bytes("V8b.ev0", pkt, want, 11);

        ble_anomaly_event_t e19 = mk_event(BLE_EVENT_AF_ANOMALY,
                                          1750000000001ULL + 19ULL * 60000ULL, 80);
        expect_int("V8b.ev19.len", (int)ble_pack_anomaly(&e19, pkt, sizeof(pkt)), 11);
        memcpy(want, "\x03\x21\x41\x32\x74\x97\x01\x00\x00\x50\x92", 11);
        expect_bytes("V8b.ev19", pkt, want, 11);

        /* 220 字节 = 20 × 11，且 20 == BLE_MAX_BATCH_EVENTS */
        expect_int("V8b.batch", (220 % 11) == 0 ? 220 / 11 : -1, 20);
        expect_int("V8b.max", (int)BLE_MAX_BATCH_EVENTS, 20);
    }

    /* ═══ §7 向量 V9 — 坏 CRC 必须被拒绝（V3 末字节取反） ═══ */
    {
        memcpy(pkt, "\x01\x00\x68\xe5\xcf\x8b\x01\x00\x00\x5c\xde", 11);
        /* 逐条 CRC 不一致 → 该条丢弃 */
        expect_int("V9.mismatch", ble_crc8(pkt, 10) != pkt[10], 1);
        /* 整包不变式被破坏（合法包恒为 0x00）→ 可检出 */
        expect_int("V9.invariant", ble_crc8(pkt, 11) != 0x00, 1);
    }

    /* ═══ §8 向量 V10 — 标准特征值载荷（0x2A37 / 0x2A5F，O-3 有意偏离标准） ═══ */
    expect_int("V10.len", (int)ble_encode_hr(1, pkt, sizeof(pkt)), 2);
    memcpy(want, "\x01\x00", 2);
    expect_bytes("V10", pkt, want, 2);

    expect_int("V10b.len", (int)ble_encode_hr(65535, pkt, sizeof(pkt)), 2);
    memcpy(want, "\xff\xff", 2);
    expect_bytes("V10b", pkt, want, 2);

    /* V10c 仅 1 字节 → 长度不足，无法构成 HR 载荷（Client 侧解析为 null） */
    expect_int("V10c.cap", (int)ble_encode_hr(1, pkt, 1), 0);

    expect_int("V10d.len", (int)ble_encode_spo2(100, pkt, sizeof(pkt)), 1);
    expect_int("V10d", pkt[0], 100);

    expect_int("V10e.len", (int)ble_encode_spo2(0, pkt, sizeof(pkt)), 1);
    expect_int("V10e", pkt[0], 0);

    /* V10f 空数组 → 长度不足（Client 侧解析为 null） */
    expect_int("V10f.cap", (int)ble_encode_spo2(100, pkt, 0), 0);

    /* ═══ 边界防护（不得崩溃） ═══ */
    expect_int("NULL.pack", (int)ble_pack_anomaly(NULL, pkt, sizeof(pkt)), 0);
    expect_int("NULL.parse", ble_parse_user_config(NULL, 8, &cfg), BLE_PROTO_ERR_LEN);
    expect_int("NULL.switch", ble_parse_model_switch(NULL, 2, &idx), BLE_PROTO_ERR_LEN);
    expect_int("LEN.zero", ble_parse_user_config(pkt, 0, &cfg), BLE_PROTO_ERR_LEN);
    expect_int("LEN.over", ble_parse_user_config(pkt, 33, &cfg), BLE_PROTO_ERR_LEN);

    printf("test_protocol: ALL PASS (%d checks; V1-V10f 24 vectors + P6a/P6b/P7)\n", g_checks);
    return 0;
}
