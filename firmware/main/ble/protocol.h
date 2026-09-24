#pragma once
/* 协议纯编解码（Tianshang 自定义 GATT 线格式）。
 *
 * 本模块**不得**依赖 NimBLE / ESP-IDF 头（只允许 <stdint.h> / <stddef.h> / <string.h>），
 * 以便在 host 上用普通 C 编译器直接编译并跑契约测试（tests/host/test_protocol.c）。
 *
 * 线格式唯一真源：docs/PROTOCOL.md + docs/PROTOCOL_VECTORS.md（本模块只实现，不定义）。
 * 代码与向量冲突时以向量为准；禁止在本模块引入第二份期望值。
 */

#include <stdint.h>
#include <stddef.h>

/* ── 线格式长度常量（对应 docs/PROTOCOL_VECTORS.md，禁魔法数） ── */
#define BLE_ANOMALY_PKT_LEN    11u   /* 0xFFF2: type[1] + ts[8 LE] + conf[1] + crc8[1] */
#define BLE_ANOMALY_DATA_LEN   10u   /* 0xFFF2 CRC 覆盖范围 */
#define BLE_USER_CFG_PKT_LEN   8u    /* 0xFFF1: hr[2 LE] + spo2[1] + gender[1] + age[2 LE] + reserved[1] + crc8[1] */
#define BLE_USER_CFG_DATA_LEN  7u    /* 0xFFF1 CRC 覆盖范围（含 reserved） */
#define BLE_USER_CFG_MAX_LEN   32u   /* 0xFFF1 固件接受的写入上限（见 open_items D-B） */
#define BLE_MODEL_SWITCH_LEN   2u    /* 0xFFF3: idx[1] + crc8[1] */
#define BLE_HR_PKT_LEN         2u    /* 0x2A37: uint16 LE，无 flags 字节（O-3 有意偏离） */
#define BLE_SPO2_PKT_LEN       1u    /* 0x2A5F: uint8，非标准 PLX（O-3 有意偏离） */
#define BLE_MAX_BATCH_EVENTS   20u   /* 0xFFF4: 单次 Read ≤20 条 */

/* 解析返回值（不依赖 esp_err_t，host 可直接使用） */
#define BLE_PROTO_OK           0
#define BLE_PROTO_ERR_LEN    (-1)   /* 长度不合法 */
#define BLE_PROTO_ERR_CRC    (-2)   /* CRC-8 校验失败 */
#define BLE_PROTO_ERR_RANGE  (-3)   /* 字段越界（如 idx > 1） */

typedef enum {
    BLE_EVENT_ANOMALY_HR,       // 心率异常
    BLE_EVENT_ANOMALY_SPO2,     // 血氧异常
    BLE_EVENT_MODEL_SWITCH,     // 模型切换
    BLE_EVENT_AF_ANOMALY = 3,   // AF 异常（房颤检出，匹配 PROTOCOL.md §4）
} ble_event_type_t;

typedef struct {
    ble_event_type_t type;
    uint64_t timestamp_ms;
    uint8_t confidence;         // 0-100
} ble_anomaly_event_t;

/* APP→Watch 用户配置（PROTOCOL.md §3，0xFFF1） */
typedef struct {
    uint16_t hr_threshold_bpm;
    uint8_t  spo2_threshold_pct;
    uint8_t  gender;            // 0=女 1=男
    uint16_t age;
} ble_user_config_t;

/* ── CRC-8：poly 0x07 / init 0x00 / 无反射 / 无最终异或（PROTOCOL_VECTORS §1） ── */
uint8_t ble_crc8(const uint8_t *data, size_t len);

/* ── 0xFFF2 异常事件 ── */
size_t ble_pack_anomaly(const ble_anomaly_event_t *event, uint8_t *out, size_t cap);

/* ── 0xFFF1 用户配置 ── */
size_t ble_build_user_config(uint16_t hr_bpm, uint8_t spo2_pct, uint8_t gender,
                             uint16_t age, uint8_t *out, size_t cap);
int    ble_parse_user_config(const uint8_t *in, size_t len, ble_user_config_t *out);

/* ── 0xFFF3 模型切换 ── */
size_t ble_build_model_switch(uint8_t idx, uint8_t *out, size_t cap);
int    ble_parse_model_switch(const uint8_t *in, size_t len, uint8_t *out_idx);

/* ── 0x2A37 / 0x2A5F 标准特征值载荷（O-3 有意偏离 BLE 标准结构，禁止单端"修正"） ── */
size_t ble_encode_hr(uint16_t bpm, uint8_t *out, size_t cap);
size_t ble_encode_spo2(uint8_t pct, uint8_t *out, size_t cap);
