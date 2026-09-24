#include <math.h>

#include "sensors/ppg_preprocess.h"

/* 与 scripts/datasets/base.py::zscore_clip(std_floor=1e-8, clip=3.0) 一致 */
#define PPG_ZSCORE_STD_FLOOR 1e-8
#define PPG_ZSCORE_CLIP      3.0

void ppg_preprocess_zscore_clip(const float *in, float *out, size_t n)
{
    if (!in || !out || n == 0) {
        return;
    }

    double mean = 0.0;
    for (size_t i = 0; i < n; i++) {
        mean += (double)in[i];
    }
    mean /= (double)n;

    double var = 0.0;
    for (size_t i = 0; i < n; i++) {
        double d = (double)in[i] - mean;
        var += d * d;
    }
    var /= (double)n;

    double std = sqrt(var);
    if (std < PPG_ZSCORE_STD_FLOOR) {
        std = PPG_ZSCORE_STD_FLOOR;
    }

    for (size_t i = 0; i < n; i++) {
        float v = (float)(((double)in[i] - mean) / std);
        if (v > PPG_ZSCORE_CLIP) {
            v = (float)PPG_ZSCORE_CLIP;
        } else if (v < -PPG_ZSCORE_CLIP) {
            v = (float)-PPG_ZSCORE_CLIP;
        }
        out[i] = v;
    }
}