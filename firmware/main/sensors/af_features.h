#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 从 4s@100Hz 归一化 PPG 窗提取 6 维 RRI 特征（与 scripts/train_af_model.py
 * extract_features 保持一致）：
 *   feat[0] rri_std   RR interval std (ms)
 *   feat[1] rmssd     sqrt(mean(diff(RRI)^2)) (ms)
 *   feat[2] pnn50     fraction |diff(RRI)| > 50ms
 *   feat[3] cv        RRI std / RRI mean
 *   feat[4] hr_proxy  beats/sec proxy
 *   feat[5] diff_std  std of first difference of waveform
 * 返回检测到的峰数；窗内峰 <3 时特征置 0，返回 0。 */
int af_features_extract(const float *win, size_t len, float fs,
                        float feat[6]);

#ifdef __cplusplus
}
#endif