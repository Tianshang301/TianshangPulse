"""PPG 信号质量指数（SQI）——Level 2 门控。

与固件共用同一算法规约（纯标量，INT8 友好）。输入为 4s@100Hz 归一化窗口，
输出 0..1 质量分，低于阈值应跳过该窗特征计算。

分量：
    sqi_amp_cv   : 峰幅度变异系数（0=一致，越不稳质量越差） -> 1/(1+CV)
    sqi_interval : 相邻峰间距落在生理窗 [40,200]->[600,1200ms] 的比例
    sqi_hr_plaus : 检测 HR 是否在 [30,250] bpm 生理范围
    score = sqrt(sqi_amp_cv * sqi_interval * sqi_hr_plaus)

固件对应：firmware/main/sensors/ppg_sqi.c（实现时保持此公式一致）
"""

import numpy as np
from scipy.signal import find_peaks

TARGET_FS = 100.0
MIN_BEATS = 3


def ppg_sqi(window, fs=TARGET_FS, min_dist_s=0.4):
    """返回 (score 0..1, 诊断 dict)。window 为 1-d 归一化信号。"""
    peaks, _ = find_peaks(window, distance=int(fs * min_dist_s))
    peaks = peaks.astype(np.int64)

    if peaks.size < MIN_BEATS:
        return 0.0, {"n_peaks": int(peaks.size), "why": "too_few_peaks"}

    amps = window[peaks]
    cv = float(np.std(amps) / (np.mean(amps) + 1e-8))
    sqi_amp_cv = float(1.0 / (1.0 + cv))

    rri = np.diff(peaks) / fs * 1000.0          # ms
    lo, hi = 600.0, 1200.0                       # 0.6s-1.2s 生理窗
    sqi_interval = float(np.mean((rri >= lo) & (rri <= hi)))

    hr = 60.0 * (len(peaks)) / (len(window) / fs)
    sqi_hr = float(1.0 if 30.0 <= hr <= 250.0 else 0.0)

    score = float(np.sqrt(sqi_amp_cv * sqi_interval * sqi_hr))
    return score, {
        "n_peaks": int(peaks.size),
        "hr_est": round(hr, 1),
        "amp_cv": round(cv, 4),
        "sqi_amp_cv": round(sqi_amp_cv, 4),
        "sqi_interval": round(sqi_interval, 4),
        "sqi_hr": sqi_hr,
    }