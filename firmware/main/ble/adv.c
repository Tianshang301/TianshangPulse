/* BLE 广播实现 —— 见 ble/adv.h。
 *
 * 解决 O-4 / B1：原先 start_advertising() 从不调 ble_gap_adv_set_fields/rsp_set_fields，
 * 广播包不含 0xFFF0，APP/通用工具无法按服务 UUID 发现设备。
 *
 * 切档竞争对策（关键）：慢切定时器回调只置 s_adv_mode=SLOW + ble_gap_adv_stop()（异步），
 * 不在回调里直接 adv_start；stop 完成后 NimBLE 回 BLE_GAP_EVENT_ADV_COMPLETE，由
 * gatt_server 的 on_gap_event 调 adv_restart_current() 依当前模式重启 → 规避 stop+start 立即
 * 返回 EBUSY 的竞态。
 */
#include "ble/adv.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "config.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs.h"

#define ADV_TAG "adv"

#define ADV_SLOW_ITVL_MS  1280   /* SLOW 广播间隔（AGENTS §9.3 #7；BLE_GAP_ADV_SLOW_* 本版无） */

enum { ADV_FAST, ADV_SLOW };

static int s_adv_mode = ADV_SLOW;
static adv_gap_event_cb_t s_gap_cb;
static esp_timer_handle_t s_slow_timer;
static bool s_inited = false;

/* 广播的 16-bit 服务 UUID 列表：0xFFF0（自定义）+ 0x180D（标准心率，便于通用工具瞥见） */
static const ble_uuid16_t s_adv_uuids16[] = {
    BLE_UUID16_INIT(0xFFF0),
    BLE_UUID16_INIT(0x180D),
};

static void adv_switch_to_slow_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(ADV_TAG, "FAST burst done -> SLOW");
    s_adv_mode = ADV_SLOW;
    /* 异步停广播；ADV_COMPLETE 里 adv_restart_current() 依 s_adv_mode 起 SLOW */
    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGD(ADV_TAG, "adv_stop in slow-switch rc=%d", rc);
    }
}

esp_err_t adv_init(adv_gap_event_cb_t gap_cb)
{
    if (!gap_cb) return ESP_ERR_INVALID_ARG;
    s_gap_cb = gap_cb;

    /* adv 字段：flags + 完整 16-bit UUID 列表 + 完整设备名
     * 空间核算（P1 规格）：flags 3B + uuids16 6B + name 15B = 24B ≤ 31B */
    struct ble_hs_adv_fields adv_fields = {0};
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.uuids16 = s_adv_uuids16;
    adv_fields.num_uuids16 = (uint8_t)(sizeof(s_adv_uuids16) / sizeof(s_adv_uuids16[0]));
    adv_fields.uuids16_is_complete = 1;
    adv_fields.name = (const uint8_t *)KAdvDeviceName;
    adv_fields.name_len = (uint8_t)strlen(KAdvDeviceName);
    adv_fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(ADV_TAG, "adv set_fields failed: %d", rc);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* scan response：完整设备名（兼容只抓 rsp 的客户端） */
    struct ble_hs_adv_fields rsp_fields = {0};
    rsp_fields.name = (const uint8_t *)KAdvDeviceName;
    rsp_fields.name_len = (uint8_t)strlen(KAdvDeviceName);
    rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(ADV_TAG, "adv rsp_set_fields failed: %d", rc);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!s_slow_timer) {
        const esp_timer_create_args_t args = {
            .callback = adv_switch_to_slow_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "adv_slow",
            .skip_unhandled_events = true,
        };
        esp_err_t e = esp_timer_create(&args, &s_slow_timer);
        if (e != ESP_OK) {
            ESP_LOGE(ADV_TAG, "timer create: %s", esp_err_to_name(e));
            return e;
        }
    }
    s_inited = true;
    ESP_LOGI(ADV_TAG, "adv fields set: name=%s uuids16=[0xFFF0,0x180D]", KAdvDeviceName);
    return ESP_OK;
}

static int adv_start_raw(int mode)
{
    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    if (mode == ADV_FAST) {
        p.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
        p.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;
    } else {
        p.itvl_min = BLE_GAP_ADV_ITVL_MS(ADV_SLOW_ITVL_MS);
        p.itvl_max = BLE_GAP_ADV_ITVL_MS(ADV_SLOW_ITVL_MS);
    }
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &p, s_gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(ADV_TAG, "adv_start(%s) failed: %d", mode == ADV_FAST ? "FAST" : "SLOW", rc);
    }
    return rc;
}

esp_err_t adv_start(bool fast_burst)
{
    if (!s_inited || !s_gap_cb) {
        ESP_LOGE(ADV_TAG, "adv_start before adv_init");
        return ESP_ERR_INVALID_STATE;
    }
    s_adv_mode = fast_burst ? ADV_FAST : ADV_SLOW;
    int rc = adv_start_raw(s_adv_mode);
    if (rc != 0) return ESP_ERR_INVALID_RESPONSE;

    if (s_slow_timer) esp_timer_stop(s_slow_timer);
    if (fast_burst) {
        esp_err_t e = esp_timer_start_once(s_slow_timer,
                                           (uint64_t)KAdvFastBurstMs * 1000ULL);
        if (e != ESP_OK) {
            ESP_LOGW(ADV_TAG, "slow timer start: %s", esp_err_to_name(e));
        }
    }
    return ESP_OK;
}

esp_err_t adv_restart_current(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    int rc = adv_start_raw(s_adv_mode);
    return rc == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t adv_stop(void)
{
    if (s_slow_timer) esp_timer_stop(s_slow_timer);
    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGD(ADV_TAG, "adv_stop rc=%d (nonzero often = already stopped)", rc);
    }
    return ESP_OK;
}
