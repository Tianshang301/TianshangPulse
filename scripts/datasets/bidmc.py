"""BIDMC PPG and Respiration Dataset (PhysioNet bidmc v1.0.0) 适配器。

- 53 名重症患者，8 分钟/段，PPG + ECG(II) + 阻抗呼吸信号，125 Hz
- 手工标注为"呼吸"（.breath），无逐拍 PPG 峰金标准；
  参考心跳由 ECG 导联经简单 R 峰检测得到（本仓库自行提取，非数据集官方标注）
- 许可 ODC-BY-1.0（开放）
- 注意：页面标注可能受地区限制（403），适配器已处理为空加载。

下载：https://physionet.org/content/bidmc/1.0.0/
结构（每段）：bidmc##.hea / .dat / .breath
"""

import os

import numpy as np
import wfdb
from scipy.signal import find_peaks

from .base import BaseAdapter, Seg

LOCAL_DIR = os.path.join("data", "raw", "bidmc")


def detect_ecg_r_peaks(ecg, fs=125.0):
    """简易 ECG R 峰检测：带通 + 自适应阈值 + 最小间距 0.3s。"""
    from scipy.signal import butter, filtfilt
    nyq = fs / 2.0
    b, a = butter(2, [5.0 / nyq, 25.0 / nyq], btype="band")
    f = filtfilt(b, a, ecg)
    thr = 0.6 * np.std(f)
    dist = int(0.3 * fs)
    peaks, _ = find_peaks(f, height=thr, distance=dist)
    return peaks.astype(np.int64)


class BidmcAdapter(BaseAdapter):
    source = "bidmc"
    license = "ODC-BY-1.0"
    fs = 125.0
    description = "BIDMC PPG and Respiration Dataset (Pimentel 2016)"

    def __init__(self, root=None):
        self.root = root or LOCAL_DIR

    def load(self):
        recs = sorted(f[:-4] for f in os.listdir(self.root)
                      if f.endswith(".hea") and not f.endswith("n.hea"))
        if not recs:
            raise FileNotFoundError(
                f"no BIDMC records in {self.root} (可能受地区限制，下载被 403)；"
                "可从 https://physionet.org/content/bidmc/1.0.0/ 获取")

        segs = []
        for name in recs:
            rec = wfdb.rdrecord(os.path.join(self.root, name))
            chans = [c.lower() for c in rec.sig_name]
            ppg_idx = next((i for i, c in enumerate(chans)
                            if "pleth" in c or "ppg" in c), 0)
            ecg_idx = next((i for i, c in enumerate(chans)
                            if "ecg" in c or "ekg" in c), None)
            ppg = rec.p_signal[:, ppg_idx]
            ecg = rec.p_signal[:, ecg_idx] if ecg_idx is not None else None
            ref = detect_ecg_r_peaks(ecg) if ecg is not None else np.array([], dtype=np.int64)
            segs.append(Seg(
                name=name, fs=self.fs, ppg=ppg.astype(np.float64),
                ref_beats=ref, patient_id=f"bidmc_{name}",
                label="non_af" if "af" not in name.lower() else "af",
            ))
        return segs, {
            "source": self.source, "license": self.license,
            "fs": self.fs, "n_segments": len(segs),
            "note": "ref_beats 由 ECG 自检，非官方标注",
            "description": self.description,
        }
