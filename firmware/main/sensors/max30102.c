#include <math.h>
#include <stdint.h>
#include <string.h>

#include "sensors/max30102.h"
#include "sensors/sensor_i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"

#define TAG "max30102"

#define MAX30102_FIFO_DEPTH       32
#define MAX30102_OVF_WARN_US      (30LL * 1000000LL)   // 溢出告警节流周期 30s

static i2c_master_dev_handle_t s_dev = NULL;
static float s_last_ir = 0.0f;        // FIFO 空时保持上一帧
static uint8_t s_last_ovf = 0;        // 上次 OVF_COUNTER 读数（4-bit 环绕）
static uint32_t s_ovf_lost = 0;       // 累计丢失样本数
static int64_t s_last_ovf_log_us = 0;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

/* 一次事务突发读 0x04..0x06：FIFO_WR_PTR / OVF_COUNTER / FIFO_RD_PTR */
static esp_err_t fifo_status(uint8_t *wr, uint8_t *ovf, uint8_t *rd)
{
    uint8_t buf[3];
    esp_err_t ret = i2c_master_transmit_receive(s_dev,
                                                (const uint8_t[]){MAX30102_REG_FIFO_WR_PTR}, 1,
                                                buf, sizeof(buf), 100);
    if (ret != ESP_OK) return ret;
    *wr = buf[0];
    *ovf = buf[1] & 0x0F;
    *rd = buf[2];
    return ESP_OK;
}

/* 批量读 n 帧 FIFO 数据（每帧 6 字节：RED[3] + IR[3]，18-bit 高位在前） */
static esp_err_t fifo_read_n(uint32_t *red, uint32_t *ir, size_t n)
{
    uint8_t buf[MAX30102_FIFO_DEPTH * 6];   // n <= 32（FIFO 深度）
    esp_err_t ret = i2c_master_transmit_receive(s_dev,
                                                (const uint8_t[]){MAX30102_REG_FIFO_DATA}, 1,
                                                buf, n * 6, 100);
    if (ret != ESP_OK) return ret;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *p = &buf[i * 6];
        uint32_t r = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        uint32_t v = ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5];
        red[i] = r & 0x3FFFF;
        ir[i]  = v & 0x3FFFF;
    }
    return ESP_OK;
}

#if CONFIG_SENSOR_SIM_PPG
typedef struct {
    uint32_t sample;   // 全局样本计数
    uint32_t t_beat;   // 当前 beat 内样本偏移
    uint32_t beat_len; // 当前 beat 长度（样本 @100Hz）
    uint32_t beat_idx; // AF 模式下的 beat 序号
} sim_ctx_t;

static sim_ctx_t s_sim = {0};

/* 窦性 75bpm => beat_len=80 样本；AF：不规则 RR + 较快平均心室率 */
static const uint32_t kAfBeatLens[] = {65, 110, 55, 95, 70, 105, 60, 115};

static float sim_waveform(void)
{
    uint32_t len = s_sim.beat_len;
    float t = (float)s_sim.t_beat;
    /* 每 beat 单个高斯峰（t=6 峰值），平滑衰减无次峰，峰间距 = beat_len */
    float dx = t - 6.0f;
    float v = 1.0f + 1.2f * expf(-(dx * dx) / 18.0f);
    if (s_sim.t_beat >= len) {
        s_sim.t_beat = 0;
        s_sim.beat_idx++;
#if CONFIG_SENSOR_SIM_PPG_AF
        s_sim.beat_len = kAfBeatLens[s_sim.beat_idx % (sizeof(kAfBeatLens) / sizeof(kAfBeatLens[0]))];
#else
        s_sim.beat_len = 100;   // 60 bpm（休息态窦性）
#endif
        v = 1.0f;
    }
    return v;
}
#endif /* CONFIG_SENSOR_SIM_PPG */

esp_err_t max30102_init(void)
{
#if CONFIG_SENSOR_SIM_PPG
    ESP_LOGW(TAG, "simulated PPG enabled, MAX30102 hardware access skipped");
    memset(&s_sim, 0, sizeof(s_sim));
#if CONFIG_SENSOR_SIM_PPG_AF
    s_sim.beat_len = kAfBeatLens[0];
#else
    s_sim.beat_len = 100;   // 60 bpm（休息态窦性）
#endif
    return ESP_OK;
#else
    esp_err_t ret = sensor_i2c_add_device(MAX30102_I2C_ADDR, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "dev add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t pid = 0;
    if (reg_read(MAX30102_REG_PART_ID, &pid) == ESP_OK) {
        ESP_LOGI(TAG, "part id=0x%02x (expect 0x%02x)", pid, MAX30102_EXPECTED_PART_ID);
        if (pid != MAX30102_EXPECTED_PART_ID) {
            ESP_LOGW(TAG, "unexpected part id, continuing anyway");
        }
    } else {
        ESP_LOGW(TAG, "part id read failed (sensor absent?)");
    }

    /* 寄存器配置：FIFO rollover + SpO2 模式(双 LED) + 100sps + LED 电流 */
    reg_write(MAX30102_REG_FIFO_CFG, 0x4F);
    reg_write(MAX30102_REG_MODE_CFG, MAX30102_MODE_SPO2);
    reg_write(MAX30102_REG_SPO2_CFG, MAX30102_SPO2_CFG_100SPS);
    reg_write(MAX30102_REG_LED1_PA, 0x24);
    reg_write(MAX30102_REG_LED2_PA, 0x24);
    return ESP_OK;
#endif
}

esp_err_t max30102_read_ppg(sensor_ppg_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    out->heart_rate_bpm = 0;
    out->blood_oxygen_pct = 0;
#if CONFIG_SENSOR_SIM_PPG
    out->raw_ir = sim_waveform();
    s_sim.t_beat++;
    s_sim.sample++;
    s_last_ir = out->raw_ir;
    return ESP_OK;
#else
    if (!s_dev) {
        /* 设备不存在/总线未初始化：保持上帧不崩溃（对齐 mpu6886 防护） */
        out->raw_ir = s_last_ir;
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t wr = 0, ovf = 0, rd = 0;
    esp_err_t ret = fifo_status(&wr, &ovf, &rd);
    if (ret != ESP_OK) {
        out->raw_ir = s_last_ir;
        return ret;
    }

    /* 溢出检测：OVF_COUNTER 4-bit 环绕，差值即本轮新丢失样本数 */
    if (ovf != s_last_ovf) {
        s_ovf_lost += (uint8_t)((ovf - s_last_ovf) & 0x0F);
        s_last_ovf = ovf;
        int64_t now = esp_timer_get_time();
        if (now - s_last_ovf_log_us > MAX30102_OVF_WARN_US) {
            ESP_LOGW(TAG, "fifo overflow, %u samples lost (poll slower than 100sps?)",
                     (unsigned)s_ovf_lost);
            s_last_ovf_log_us = now;
        }
    }

    uint8_t avail = (uint8_t)((wr - rd) & 0x1F);
    if (avail == 0) {
        out->raw_ir = s_last_ir;   // FIFO 空：无新样本（定时对齐时的常态）
        return ESP_OK;
    }

    uint32_t red[MAX30102_FIFO_DEPTH], ir[MAX30102_FIFO_DEPTH];
    ret = fifo_read_n(red, ir, avail);
    if (ret != ESP_OK) {
        out->raw_ir = s_last_ir;
        return ret;
    }
    uint32_t last_ir = ir[avail - 1];   // 取最新帧（偶发积压时相位优先）
    float v = (last_ir > 0) ? ((float)last_ir / 32768.0f - 1.0f) : s_last_ir;
    out->raw_ir = v;
    s_last_ir = v;
    return ESP_OK;
#endif
}

esp_err_t max30102_deinit(void)
{
    s_dev = NULL;   // 共享总线由 sensor_i2c_deinit() 统一销毁
    return ESP_OK;
}