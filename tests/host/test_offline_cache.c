/* offline_cache 环形缓冲语义 host 单测（与 firmware/main/ble/offline_cache.c
 * 同源编译）。覆盖：
 *   - push / count 基础语义
 *   - peek_batch 最旧优先 + 预览不删除
 *   - pop 精确删除（A5 修复：ACK 只清已读，不再全清）
 *   - pop 超量裁剪 / flush 全清
 *   - 容量满覆盖最旧（KMaxOfflineEvents=100）
 *   - NULL/边界防护
 * 编译（zig 任意主机）：见 scripts 或
 *   python -m ziglang cc -O2 -I tests/host/stubs -I firmware/main \
 *       tests/host/test_offline_cache.c firmware/main/ble/offline_cache.c -o test.exe
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble/offline_cache.h"

static ble_anomaly_event_t mk(uint64_t ts)
{
    ble_anomaly_event_t e;
    e.type = BLE_EVENT_AF_ANOMALY;
    e.timestamp_ms = ts;
    e.confidence = 90;
    return e;
}

int main(void)
{
    ble_anomaly_event_t buf[128];
    size_t got = 0;

    assert(offline_cache_init() == ESP_OK);
    assert(offline_cache_count() == 0);

    /* ACK 精确清除：25 条入队 -> peek 20（最旧优先）-> pop 20 -> 剩 5 */
    for (uint64_t i = 0; i < 25; i++) {
        ble_anomaly_event_t e = mk(i + 1);
        assert(offline_cache_push(&e) == ESP_OK);
    }
    assert(offline_cache_count() == 25);

    got = 0;
    assert(offline_cache_peek_batch(buf, 20, &got) == ESP_OK);
    assert(got == 20);
    assert(buf[0].timestamp_ms == 1 && buf[19].timestamp_ms == 20);

    assert(offline_cache_pop(20) == ESP_OK);
    assert(offline_cache_count() == 5);

    got = 0;
    assert(offline_cache_peek_batch(buf, 20, &got) == ESP_OK);
    assert(got == 5);
    assert(buf[0].timestamp_ms == 21 && buf[4].timestamp_ms == 25);

    /* peek 不删除 */
    got = 0;
    assert(offline_cache_peek_batch(buf, 20, &got) == ESP_OK);
    assert(got == 5 && offline_cache_count() == 5);

    /* pop 超量裁剪 */
    assert(offline_cache_pop(99) == ESP_OK);
    assert(offline_cache_count() == 0);

    /* 容量满覆盖最旧：入队 103 -> 剩 100（ts=4..103） */
    for (uint64_t i = 0; i < 103; i++) {
        ble_anomaly_event_t e = mk(i + 1);
        assert(offline_cache_push(&e) == ESP_OK);
    }
    assert(offline_cache_count() == 100);
    got = 0;
    assert(offline_cache_peek_batch(buf, 100, &got) == ESP_OK);
    assert(got == 100);
    assert(buf[0].timestamp_ms == 4 && buf[99].timestamp_ms == 103);

    /* flush 全清 */
    assert(offline_cache_flush() == ESP_OK);
    assert(offline_cache_count() == 0);

    /* NULL / 边界防护 */
    assert(offline_cache_push(NULL) == ESP_ERR_INVALID_STATE);
    assert(offline_cache_peek_batch(NULL, 10, &got) == ESP_ERR_INVALID_STATE);
    assert(offline_cache_peek_batch(buf, 10, NULL) == ESP_ERR_INVALID_STATE);

    printf("test_offline_cache: ALL PASS\n");
    return 0;
}