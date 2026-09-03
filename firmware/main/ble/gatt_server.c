#include <string.h>
#include "ble/gatt_server.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "config.h"

#define TAG "gatt"

static const char *kDeviceName = "TianshangPulse";

static uint16_t s_hr_val_handle;
static uint16_t s_spo2_val_handle;
static uint16_t s_anomaly_val_handle;

static int on_gap_event(struct ble_gap_event *event, void *arg);

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
                .access_cb = NULL,
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
                .access_cb = NULL,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF4),   // 批量数据同步
                .access_cb = NULL,
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
    }
}

static int on_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_ADV_COMPLETE: {
        struct ble_gap_adv_params adv_params = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
        };
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &adv_params, on_gap_event, NULL);
        break;
    }
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
    return ble_gatts_notify_custom(NULL, s_hr_val_handle,
                                   (uint8_t *)&bpm, sizeof(bpm));
}

esp_err_t ble_notify_spo2(uint8_t pct)
{
    return ble_gatts_notify_custom(NULL, s_spo2_val_handle, &pct, sizeof(pct));
}

esp_err_t ble_notify_anomaly(const ble_anomaly_event_t *event)
{
    if (!event) return ESP_ERR_INVALID_ARG;
    return ble_gatts_notify_custom(NULL, s_anomaly_val_handle,
                                   (const uint8_t *)event, sizeof(*event));
}

esp_err_t ble_deinit(void)
{
    nimble_port_deinit();
    return ESP_OK;
}
