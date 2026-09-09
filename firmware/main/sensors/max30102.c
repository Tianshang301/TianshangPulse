#include <math.h>
#include <stdint.h>
#include <string.h>

#include "sensors/max30102.h"
#include "sensors/sensor_i2c.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#define TAG "max30102"

static i2c_master_dev_handle_t s_dev = NULL;
static float s_last_ir = 0.0f;      // FIFO 空时保持上一帧

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

/* 读一帧 SpO2 FIFO 数据（6 字节：RED[3] + IR[3]，18-bit 高位在前） */
static esp_err_t fifo_read(uint32_t *red, uint32_t *ir)
{
    uint8_t buf[6];
    esp_err_t ret = i2c_master_transmit_receive(s_dev,
                                                (const uint8_t[]){MAX30102_REG_FIFO_DATA}, 1,
                                                buf, sizeof(buf), 100);
    if (ret != ESP_OK) return ret;
    uint32_t r = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    uint32_t i = ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5];
    *red = r & 0x3FFFF;
    *ir  = i & 0x3FFFF;
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
    uint32_t red = 0, ir = 0;
    esp_err_t ret = fifo_read(&red, &ir);
    if (ret != ESP_OK) {
        out->raw_ir = s_last_ir;   // 读取失败保持上一帧
        return ret;
    }
    float v = (ir > 0) ? ((float)ir / 32768.0f - 1.0f) : s_last_ir;
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