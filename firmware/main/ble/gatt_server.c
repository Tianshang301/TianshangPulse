#include <string.h>
#include "ble/gatt_server.h"
#include "ble/protocol.h"
#include "ble/adv.h"
#include "ble/offline_cache.h"
#include "power/power_manager.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "config.h"

#define TAG "gatt"

/* 线格式长度常量与 CRC-8 / 打包函数已迁至 ble/protocol.h + protocol.c（P4「行为不变」）。
 * 广播载荷与 FAST→SLOW 策略见 ble/adv.{c,h}（O-4 / P6）。设备名用 config.h 的 KAdvDeviceName。 */

static uint16_t s_hr_val_handle;
static uint16_t s_spo2_val_handle;
static uint16_t s_anomaly_val_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static ble_user_config_t s_user_config;
static uint8_t s_model_index;        // 0=A(默认) 1=B

/* 0xFFF4 batch sync: events actually delivered by the last Read (cleared on APP ack) */
static size_t s_batch_pending = 0;

static int on_gap_event(struct ble_gap_event *event, void *arg);
static int on_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);
static int on_notify_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_db[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180D),   // Heart Rate
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(0x2A37),   // 实时心率 BPM
                .access_cb = on_notify_chr_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_hr_val_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A5F),   // 实时血氧 %
                .access_cb = on_notify_chr_access,
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
                .access_cb = on_notify_chr_access,
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
    /* GATT DB 与设备名已在 ble_gatt_server_init() 注册（须在 host 启动前）。
     * 这里只在 host 同步后起播广播（FAST burst）。预置广播字段须在广播停止态
     * 调用，sync 时刻广播尚未起，满足前提。失败不 abort：协议栈仍可运行，
     * 仅广播不可见，便于排查。 */
    if (adv_init(on_gap_event) != ESP_OK) {
        ESP_LOGE(TAG, "adv_init failed; advertising will be unavailable");
        return;
    }
    adv_start(true);
}

static int on_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            power_set_mode(POWER_MODE_ACTIVE);   /* 连接即全速 */
            adv_stop();                          /* 连接态不再切档：取消慢切定时器 */
            ESP_LOGI(TAG, "connected, handle=%u", (unsigned)s_conn_handle);
        } else {
            adv_start(true);                     /* 连接失败：重新 FAST burst */
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected");
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        power_set_mode(POWER_MODE_LIGHT_SLEEP);   /* 断开即降频浅睡 */
        adv_start(true);                          /* 断开重连给一次 FAST burst（P6） */
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* 慢切定时器驱动的 stop 完成后这里依当前模式重启为 SLOW；
         * 自然完成则按原模式重启。规避 stop+start 立即 EBUSY 竞态。 */
        adv_restart_current();
        break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        break;
    default:
        break;
    }
    return 0;
}

/* NimBLE host 事件循环任务。
 * nimble_port_run() 是 host 的事件泵（IDF: while(1) + 永久等待事件），**永不返回**，
 * 因此必须跑在独立任务里；早前版本在 app_main 里直接调用它，会把 app_main 永久
 * 卡在 BLE 初始化处（表现为日志停在 adv 启动后、无 panic，其后 offline_cache /
 * ui_init / 任务创建全部不执行）。nimble_port_stop() 后本函数返回并清理任务。 */
static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_gatt_server_init(void)
{
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        return rc;
    }
    ble_hs_cfg.sync_cb = on_sync;

    /* 注册 GAP / GATT 内建服务（须在 nimble_port_run 之前；否则设备名/0x1801
     * 服务不存在，ble_svc_gap_device_name_set 与 ble_gatts_add_svcs 会失败 ——
     * 即曾导致 on_sync 里 "gatt config failed: ERROR" 的根因）。 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* 设备名 + 自定义 GATT DB 注册：count → add，均须在 host 启动前完成。 */
    int gatt_rc = ble_svc_gap_device_name_set(KAdvDeviceName);
    ESP_LOGI(TAG, "gap_device_name_set rc=%d", gatt_rc);
    if (gatt_rc == 0) {
        gatt_rc = ble_gatts_count_cfg(gatt_db);
        ESP_LOGI(TAG, "gatts_count_cfg rc=%d", gatt_rc);
    }
    if (gatt_rc == 0) {
        gatt_rc = ble_gatts_add_svcs(gatt_db);
        ESP_LOGI(TAG, "gatts_add_svcs rc=%d", gatt_rc);
    }
    if (gatt_rc != 0) {
        ESP_LOGE(TAG, "gatt config failed: rc=%d", gatt_rc);
        return ESP_FAIL;
    }

    /* host 事件循环改为独立任务：app_main 立即返回，继续 offline_cache / UI / 任务创建 */
    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

esp_err_t ble_notify_heart_rate(uint16_t bpm)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t pkt[BLE_HR_PKT_LEN];
    if (ble_encode_hr(bpm, pkt, sizeof(pkt)) != BLE_HR_PKT_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, sizeof(pkt));
    if (!om) return ESP_ERR_NO_MEM;
    int rc = ble_gatts_notify_custom(s_conn_handle, s_hr_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t ble_notify_spo2(uint8_t pct)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t pkt[BLE_SPO2_PKT_LEN];
    if (ble_encode_spo2(pct, pkt, sizeof(pkt)) != BLE_SPO2_PKT_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, sizeof(pkt));
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
    uint8_t buf[BLE_USER_CFG_MAX_LEN];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < BLE_USER_CFG_PKT_LEN || len > sizeof(buf)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, len, buf) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    int prc = ble_parse_user_config(buf, len, &s_user_config);
    if (prc == BLE_PROTO_ERR_CRC) {
        return BLE_ATT_ERR_INVALID_PDU;   // CRC-8 校验失败
    }
    if (prc != BLE_PROTO_OK) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    ESP_LOGI(TAG, "config: hr_thr=%u spo2_thr=%u gender=%u age=%u",
             (unsigned)s_user_config.hr_threshold_bpm,
             (unsigned)s_user_config.spo2_threshold_pct,
             s_user_config.gender, (unsigned)s_user_config.age);
    return 0;
}

/* ==== 0xFFF3 模型切换 Write（PROTOCOL.md §5） ==== */
static int on_chr_fff3_write(struct ble_gatt_access_ctxt *ctxt)
{
    uint8_t buf[1];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (os_mbuf_copydata(ctxt->om, 0, 1, buf) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    int prc = ble_parse_model_switch(buf, len, &s_model_index);
    if (prc == BLE_PROTO_ERR_RANGE) {
        return BLE_ATT_ERR_INVALID_PDU;
    }
    if (prc != BLE_PROTO_OK) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    ESP_LOGI(TAG, "model switch -> %s", s_model_index == 0 ? "A (default)" : "B");
    return 0;
}

/* ==== 0xFFF4 批量数据同步（PROTOCOL.md §6） ==== */
static int on_chr_fff4_access(uint16_t conn_handle, struct ble_gatt_access_ctxt *ctxt)
{
    (void)conn_handle;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        /* 预览最旧 <=20 条（PROTOCOL.md §6）但不删除：APP ACK（Write）时才
         * 精确清除本次交付的条数——单批装不下的剩余事件留待下轮 Read。 */
        ble_anomaly_event_t ev[BLE_MAX_BATCH_EVENTS];
        size_t got = 0;
        size_t appended = 0;
        s_batch_pending = 0;
        offline_cache_peek_batch(ev, BLE_MAX_BATCH_EVENTS, &got);
        for (size_t i = 0; i < got; i++) {
            uint8_t pkt[BLE_ANOMALY_PKT_LEN];
            if (ble_pack_anomaly(&ev[i], pkt, sizeof(pkt)) == 0) break;
            if (os_mbuf_append(ctxt->om, pkt, sizeof(pkt)) != 0) break;  // MTU 满截断
            appended++;
        }
        s_batch_pending = appended;
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (s_batch_pending > 0) {
            offline_cache_pop(s_batch_pending);
            ESP_LOGI(TAG, "batch ack: cleared %u events, %u remain",
                     (unsigned)s_batch_pending, (unsigned)offline_cache_count());
        } else {
            ESP_LOGI(TAG, "batch ack without pending read (ignored)");
        }
        s_batch_pending = 0;
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

/* Notify-only 特征（0x2A37 / 0x2A5F / 0xFFF2）没有可读可写的值语义，但 NimBLE
 * 要求每个特征都注册非 NULL 的 access_cb（ble_gatts_chr_is_sane 把
 * access_cb==NULL 判为 BLE_HS_EINVAL，令 ble_gatts_count_cfg 直接失败）。
 * 该回调仅让 count/add 通过；值仍由 ble_gatts_notify 独立发布。
 * 若客户端真的读进来，返回 READ_NOT_PERMITTED（比 UNLIKELY 语义准确）。 */
static int on_notify_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
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
