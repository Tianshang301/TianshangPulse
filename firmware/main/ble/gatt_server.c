#include <string.h>
#include "ble/gatt_server.h"
#include "ble/offline_cache.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "config.h"

#define TAG "gatt"

#define BLE_ANOMALY_PKT_LEN  11      // type[1] + ts[8 LE] + conf[1] + crc8
#define BLE_ANOMALY_DATA_LEN 10      // CRC 覆盖的前 10 字节
#define BLE_USER_CFG_DATA_LEN 7      // PROTOCOL.md §3: hr[2]+spo2[1]+gender[1]+age[2]
#define BLE_MAX_BATCH_EVENTS 20      // PROTOCOL.md §6: Read 一次 ≤20 条

static const char *kDeviceName = "TianshangPulse";

static uint16_t s_hr_val_handle;
static uint16_t s_spo2_val_handle;
static uint16_t s_anomaly_val_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static ble_user_config_t s_user_config;
static uint8_t s_model_index;        // 0=A(默认) 1=B

static int on_gap_event(struct ble_gap_event *event, void *arg);
static int on_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);
static void start_advertising(void);

/* CRC-8：多项式 0x07，初值 0x00（PROTOCOL.md §1） */
static uint8_t ble_crc8(const uint8_t *data, size_t len)
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
static size_t ble_pack_anomaly(const ble_anomaly_event_t *event, uint8_t *out, size_t cap)
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

static const struct ble_gatt_svc_def gatt_db[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180D),   // Heart Rate
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(0x2A37),   // 实时心率 BPM
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_hr_val_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A5F),   // 实时血氧 %
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_spo2_val_handle,
            },
            {0},
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFF0),   // Tianshang custom service
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF1),   // APP->Watch 用户配置
                .access_cb = on_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF2),   // Watch->APP 异常事件
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_anomaly_val_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF3),   // 模型 A/B 切换
                .access_cb = on_chr_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF4),   // 批量数据同步
                .access_cb = on_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {0},
        },
    },
    {0},
};

static void on_sync(void)
{
    esp_err_t err = ble_svc_gap_device_name_set(kDeviceName);
    if (err == 0) {
        err = ble_gatts_count_cfg(gatt_db);
    }
    if (err == 0) {
        err = ble_gatts_add_svcs(gatt_db);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gatt config failed: %s", esp_err_to_name(err));
        return;
    }
    start_advertising();
}

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN,
        .itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX,
    };
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &adv_params, on_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
    }
}

static int on_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected, handle=%u", (unsigned)s_conn_handle);
        } else {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected");
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        start_advertising();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        break;
    default:
        break;
    }
    return 0;
}

esp_err_t ble_gatt_server_init(void)
{
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        return rc;
    }
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_run();
    return ESP_OK;
}

esp_err_t ble_notify_heart_rate(uint16_t bpm)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&bpm, sizeof(bpm));
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, s_hr_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t ble_notify_spo2(uint8_t pct)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&pct, sizeof(pct));
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, s_spo2_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t ble_notify_anomaly(const ble_anomaly_event_t *event)
{
    if (!event) return ESP_ERR_INVALID_ARG;
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t pkt[BLE_ANOMALY_PKT_LEN];
    size_t n = ble_pack_anomaly(event, pkt, sizeof(pkt));
    struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, n);
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, s_anomaly_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

/* ==== 0xFFF1 用户配置 Write（PROTOCOL.md §3） ==== */
static int on_chr_fff1_write(struct ble_gatt_access_ctxt *ctxt)
{
    uint8_t buf[32];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < BLE_USER_CFG_DATA_LEN + 1 || len > sizeof(buf)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, len, buf) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (ble_crc8(buf, BLE_USER_CFG_DATA_LEN) != buf[len - 1]) {
        return BLE_ATT_ERR_INVALID_PDU;   // CRC-8 校验失败
    }
    s_user_config.hr_threshold_bpm   = (uint16_t)(buf[0] | (buf[1] << 8));
    s_user_config.spo2_threshold_pct = buf[2];
    s_user_config.gender             = buf[3];
    s_user_config.age                = (uint16_t)(buf[4] | (buf[5] << 8));
    ESP_LOGI(TAG, "config: hr_thr=%u spo2_thr=%u gender=%u age=%u",
             (unsigned)s_user_config.hr_threshold_bpm,
             (unsigned)s_user_config.spo2_threshold_pct,
             s_user_config.gender, (unsigned)s_user_config.age);
    return 0;
}

/* ==== 0xFFF3 模型切换 Write（PROTOCOL.md §5） ==== */
static int on_chr_fff3_write(struct ble_gatt_access_ctxt *ctxt)
{
    uint8_t idx = 0;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (os_mbuf_copydata(ctxt->om, 0, 1, &idx) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (idx > 1) return BLE_ATT_ERR_INVALID_PDU;
    s_model_index = idx;
    ESP_LOGI(TAG, "model switch -> %s", idx == 0 ? "A (default)" : "B");
    return 0;
}

/* ==== 0xFFF4 批量数据同步（PROTOCOL.md §6） ==== */
static int on_chr_fff4_access(uint16_t conn_handle, struct ble_gatt_access_ctxt *ctxt)
{
    (void)conn_handle;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        size_t n = offline_cache_count();
        if (n > 0) {
            ble_anomaly_event_t ev[BLE_MAX_BATCH_EVENTS];
            size_t got = 0;
            size_t max = (n < BLE_MAX_BATCH_EVENTS) ? n : BLE_MAX_BATCH_EVENTS;
            offline_cache_get_all(ev, max, &got);
            for (size_t i = 0; i < got; i++) {
                uint8_t pkt[BLE_ANOMALY_PKT_LEN];
                if (ble_pack_anomaly(&ev[i], pkt, sizeof(pkt)) == 0) break;
                if (os_mbuf_append(ctxt->om, pkt, sizeof(pkt)) != 0) break;  // MTU 满截断
            }
        }
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        offline_cache_flush();
        ESP_LOGI(TAG, "offline cache flushed (ack)");
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int on_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (!ctxt || !ctxt->chr) return BLE_ATT_ERR_UNLIKELY;
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0xFFF1)) == 0) {
        return on_chr_fff1_write(ctxt);
    }
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0xFFF3)) == 0) {
        return on_chr_fff3_write(ctxt);
    }
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0xFFF4)) == 0) {
        return on_chr_fff4_access(conn_handle, ctxt);
    }
    return BLE_ATT_ERR_UNLIKELY;
}

const ble_user_config_t *ble_get_user_config(void)
{
    return &s_user_config;
}

uint8_t ble_get_model_index(void)
{
    return s_model_index;
}

esp_err_t ble_deinit(void)
{
    nimble_port_deinit();
    return ESP_OK;
}
