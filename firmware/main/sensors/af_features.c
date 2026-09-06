#include <math.h>

#include "sensors/af_features.h"

#define AF_FEAT_MIN_PEAKS 3

static int detect_peaks(const float *s, size_t len, float fs,
                        int16_t *peaks, int max_peaks)
{
    int np = 0;
    int dist = (int)(0.4f * fs);          /* 最小峰距 400ms，对应上限 150bpm */
    for (int i = 1; i < (int)len - 1 && np < max_peaks; i++) {
        if (s[i] >= s[i - 1] && s[i] >= s[i + 1] && s[i] > 0.0f) {
            if (np == 0 || (int)(i - peaks[np - 1]) >= dist) {
                peaks[np++] = (int16_t)i;
            }
        }
    }
    return np;
}

int af_features_extract(const float *win, size_t len, float fs, float feat[6])
{
    if (!win || !feat || len < (size_t)(2.0f * fs)) {
        return 0;
    }
    for (int i = 0; i < 6; i++) feat[i] = 0.0f;

    int16_t peaks[128];
    int np = detect_peaks(win, len, fs, peaks, 128);
    if (np < AF_FEAT_MIN_PEAKS) {
        return 0;
    }

    /* RR 间期（ms） */
    int n_rri = np - 1;
    float rri[128];
    for (int i = 0; i < n_rri; i++) {
        rri[i] = (float)(peaks[i + 1] - peaks[i]) / fs * 1000.0f;
    }

    /* 均值 / 标准差 */
    float mean = 0.0f;
    for (int i = 0; i < n_rri; i++) mean += rri[i];
    mean /= n_rri;

    float var = 0.0f;
    for (int i = 0; i < n_rri; i++) {
        float d = rri[i] - mean;
        var += d * d;
    }
    var /= n_rri;
    float std_rri = sqrtf(var);

    /* rmssd */
    float sum_diff2 = 0.0f;
    int n_diff = n_rri - 1;
    for (int i = 0; i < n_diff; i++) {
        float d = rri[i + 1] - rri[i];
        sum_diff2 += d * d;
    }
    float rmssd = n_diff > 0 ? sqrtf(sum_diff2 / n_diff) : 0.0f;

    /* pnn50 */
    int cnt50 = 0;
    for (int i = 0; i < n_diff; i++) {
        if (fabsf(rri[i + 1] - rri[i]) > 50.0f) cnt50++;
    }
    float pnn50 = n_diff > 0 ? (float)cnt50 / n_diff : 0.0f;

    /* cv */
    float cv = mean > 0.0f ? std_rri / mean : 0.0f;

    /* hr_proxy = beats/sec */
    float hr_proxy = (float)np / ((float)len / fs);

    /* diff_std */
    float dm = 0.0f;
    int n_s = (int)len - 1;
    float diff[1024];
    int capped = n_s < 1024 ? n_s : 1024;
    for (int i = 0; i < capped; i++) diff[i] = win[i + 1] - win[i];
    if (capped == 0) return 0;
    for (int i = 0; i < capped; i++) dm += diff[i];
    dm /= capped;
    float dv = 0.0f;
    for (int i = 0; i < capped; i++) {
        float d = diff[i] - dm;
        dv += d * d;
    }
    dv /= capped;

    feat[0] = std_rri;
    feat[1] = rmssd;
    feat[2] = pnn50;
    feat[3] = cv;
    feat[4] = hr_proxy;
    feat[5] = sqrtf(dv);
    return np;
}