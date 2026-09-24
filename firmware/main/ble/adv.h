#pragma once
/* BLE 广播载荷与 FAST→SLOW 间隔策略（O-4 / B1 / P6）。
 *
 * 本模块依赖 NimBLE（非纯函数），不入 host 契约测试；线格式的纯编解码见 ble/protocol.{c,h}。
 * 广播字段内容非协议线格式，不触发协议升版（AGENTS §9.2 只管线格式变更）。
 *
 * 生命周期（由 gatt_server.c 调度）：
 *   on_sync:   adv_init(on_gap_event) → adv_start(true)   // 开机给一次 FAST burst
 *   CONNECT:   adv_stop()            // 取消慢切定时器，连接态不再切档
 *   DISCONNECT: adv_start(true)      // 断开重连给一次 FAST burst
 *   ADV_COMPLETE: adv_restart_current()  // 依当前模式重启
 */
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NimBLE 前向声明：避免在头里强引入 NimBLE 头，由 adv.c 内部包含真实头 */
struct ble_gap_event;

/* GAP 事件回调类型（与 NimBLE ble_gap_adv_start 的回调签名一致） */
typedef int (*adv_gap_event_cb_t)(struct ble_gap_event *event, void *arg);

/* 预置 adv / scan response 字段 + 创建慢切定时器。gap_cb 为 GAP 事件回调（gatt_server
 * 的 on_gap_event）。须在 adv 停止态调用（ble_gap_adv_set_fields 在广播中会返回 EBUSY）。 */
esp_err_t adv_init(adv_gap_event_cb_t gap_cb);

/* 起播。fast_burst=true：FAST 间隔 + 起 30s(KAdvFastBurstMs) 单次定时器后切 SLOW；
 * fast_burst=false：直接 SLOW 间隔。 */
esp_err_t adv_start(bool fast_burst);

/* 停广播 + 取消慢切定时器（best-effort，广播未运行时静默成功）。 */
esp_err_t adv_stop(void);

/* 依当前模式重启广播（ADV_COMPLETE 事件用；定时器已切 SLOW 则起 SLOW）。 */
esp_err_t adv_restart_current(void);

#ifdef __cplusplus
}
#endif
