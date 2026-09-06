#include <math.h>
#include <string.h>

#include "sensors/signal_gate.h"

/* 阈值默认值：对应 model/af/motion_gate.json 标定结果与 scripts/ppg_sqi.py 约定。
 * 固件上可根据实测微调。 */
static float s_t_silent = 0.05f;   // 低于此：正常推理
static float s_t_high = 0.0503f;   // 高于此：高运动，抑制 AF
static float s_sqi_min = 0.30f;    // 低于此：信号质量差

/* 1 级 DP 高通（a = 1 - exp(-2*pi*fc/fs)），免缓冲实现。 */
static float highpass(float x, float *prev_x, float *prev_y, float a)
{
    float y = a * (x - (*prev_x)) + (1.0f - a) * (*prev_y);
    *prev_x = x;
    *prev_y = y;
    return y;
}

void signal_gate_init(void)
{
    s_t_silent = 0.05f;
    s_t_high = 0.0503f;
    s_sqi_min = 0.30f;
}

void signal_gate_set_thresholds(float t_silent, float t_high, float sqi_min)
{
    s_t_silent = t_silent;
    s_t_high = t_high;
    s_sqi_min = sqi_min;
}

float signal_gate_motion_energy(const int16_t *ax, const int16_t *ay,
                                const int16_t *az, size_t n, uint32_t sample_rate_hz)
{
    if (!ax || !ay || !az || n < 2 || sample_rate_hz == 0) {
        return 0.0f;
    }
    const float scale = 1.0f / 4096.0f;   // +-8g
    const float fc = 0.5f;                 // 与脚本一致的高通截止
    const float a = 1.0f - expf(-2.0f * (float)M_PI * fc / (float)sample_rate_hz);

    float px = 0.0f, py = 0.0f, pz = 0.0f; // 上一输入
    float qx = 0.0f, qy = 0.0f, qz = 0.0f; // 上一输出
    double sum = 0.0, sum2 = 0.0;
    size_t cnt = 0;
    for (size_t i = 0; i < n; i++) {
        float mx = sqrtf((ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i])) * scale;
        float hx = highpass(mx, &px, &qx, a);
        sum += hx;
        sum2 += hx * hx;
        cnt++;
    }
    if (cnt == 0) return 0.0f;
    float mean = (float)(sum / (double)cnt);
    float var = (float)(sum2 / (double)cnt) - mean * mean;
    return var < 0.0f ? 0.0f : var;
}

float signal_gate_ppg_sqi(const float *win, size_t len, float fs, int *n_peaks)
{
    if (!win || len < (size_t)(0.4 * fs * 4)) {
        if (n_peaks) *n_peaks = 0;
        return 0.0f;
    }
    /* 峰值检测（幅度局部极大，最小间距 0.4s -> 与 scripts 一致） */
    int max_peaks = 64;
    int16_t peaks[64];
    int np = 0;
    int dist = (int)(0.4f * fs);
    for (int i = 1; i < (int)len - 1 && np < max_peaks; i++) {
        if (win[i] >= win[i - 1] && win[i] >= win[i + 1] && win[i] > 0.0f) {
            if (np == 0 || (int)(i - peaks[np - 1]) >= dist) {
                peaks[np++] = (int16_t)i;
            }
        }
    }
    if (n_peaks) *n_peaks = np;
    if (np < 3) return 0.0f;

    /* 峰幅度变异系数 -> sqi_amp_cv */
    float mean_amp = 0.0f;
    float mean_amp2 = 0.0f;
    for (int i = 0; i < np; i++) {
        float a = win[peaks[i]];
        mean_amp += a;
        mean_amp2 += a * a;
    }
    mean_amp /= np;
    float var_amp = (mean_amp2 / np) - mean_amp * mean_amp;
    if (var_amp < 0) var_amp = 0;
    float cv = sqrtf(var_amp) / (mean_amp + 1e-8f);
    float sqi_amp_cv = 1.0f / (1.0f + cv);

    /* 峰间距生理窗 600-1200ms */
    float ok = 0.0f;
    for (int i = 1; i < np; i++) {
        float rri_ms = (float)(peaks[i] - peaks[i - 1]) / fs * 1000.0f;
        if (rri_ms >= 600.0f && rri_ms <= 1200.0f) ok += 1.0f;
    }
    float sqi_interval = (np > 1) ? ok / (float)(np - 1) : 0.0f;

    /* HR 生理窗 */
    float hr = 60.0f * (float)np / ((float)len / fs);
    float sqi_hr = (hr >= 30.0f && hr <= 250.0f) ? 1.0f : 0.0f;

    float score = sqrtf(sqi_amp_cv * sqi_interval * sqi_hr);
    return score;
}

signal_gate_result_t signal_gate_evaluate(float motion_energy, float sqi)
{
    signal_gate_result_t r;
    r.motion_energy = motion_energy;
    r.sqi = sqi;
    if (sqi < s_sqi_min) {
        r.level = SIGNAL_GATE_LOW_SQI;
    } else if (motion_energy >= s_t_high) {
        r.level = SIGNAL_GATE_HIGH_MOTION;
    } else if (motion_energy >= s_t_silent) {
        r.level = SIGNAL_GATE_LOW_MOTION;
    } else {
        r.level = SIGNAL_GATE_ACTIVE;
    }
    return r;
}