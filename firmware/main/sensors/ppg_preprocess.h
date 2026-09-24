#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 逐窗 Z-score + clip ±3（峰值检测/特征提取/SQI 之前必须调用）。
 *
 * 与训练端预处理完全一致（scripts/datasets/base.py::zscore_clip 与
 * scripts/prepare_af_dataset.py::preprocess_window）：
 *   - mean/std 取总体统计量（population，除以 n）
 *   - std 下限 1e-8（对应 Python: if std < 1e-8: std = 1e-8）
 *   - clip 区间 [-3.0, +3.0]
 * C 侧以 double 累加 mean/var 降噪，输出 float32。
 * in/out 可为同一缓冲（原地），也可分别指定。 */
void ppg_preprocess_zscore_clip(const float *in, float *out, size_t n);

#ifdef __cplusplus
}
#endif