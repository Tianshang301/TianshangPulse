"""外部数据集适配器基类与通用预处理工具。"""

from dataclasses import dataclass, field

import numpy as np
from scipy.signal import butter, filtfilt


@dataclass
class Seg:
    """统一波形段：一个连续记录（可能切分为多个滑窗）。

    ppg 为原始采样率下的单通道信号；ref_beats 为参考心跳的采样点索引
    （来自 ECG R 波或手工标注峰，换算到 ppg 数组的采样点）。
    """
    name: str
    fs: float
    ppg: np.ndarray
    ref_beats: np.ndarray = field(default_factory=lambda: np.array([], dtype=np.int64))
    acc: np.ndarray = None          # (N, 3) 可选
    patient_id: str = ""
    label: str = ""                 # 原始节律标签（如 SR/AF/WALK/RUN/BIKE）


class BaseAdapter:
    source = "base"
    license = ""
    fs = 125.0
    description = ""

    def load(self):
        """返回 (list[Seg], dict_meta)。"""
        raise NotImplementedError


def bandpass(sig, fs, lo=0.5, hi=8.0, order=4):
    """按源采样率 fs 设计 0.5-8Hz 带通。

    注意：fs 必须是 sig 的实际采样率；若 sig 已被重采样，须用重采样后的 fs，
    否则通带边界错位（隐形参数失配）。
    """
    nyq = fs / 2.0
    b, a = butter(order, [lo / nyq, hi / nyq], btype="band")
    return filtfilt(b, a, sig)


def resample_ppg(win, src_fs, dst_fs):
    """线性插值重采样一维 PPG 段。"""
    n_src = len(win)
    n_dst = int(round(n_src * dst_fs / src_fs))
    x_src = np.linspace(0, n_src - 1, n_src)
    x_dst = np.linspace(0, n_src - 1, n_dst)
    return np.interp(x_dst, x_src, win)


def resample_beats(beats, src_fs, dst_fs):
    """参考心跳采样点由源采样率换算到目标采样率。"""
    return np.unique(np.round(beats * dst_fs / src_fs).astype(np.int64))


def zscore_clip(win, std_floor=1e-8, clip=3.0):
    win = np.asarray(win, dtype=np.float32)
    mean = np.mean(win)
    std = np.std(win)
    if std < std_floor:
        std = std_floor
    return np.clip((win - mean) / std, -clip, clip)
