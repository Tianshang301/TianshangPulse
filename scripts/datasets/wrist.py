"""Wrist PPG During Exercise (PhysioNet WRIST v1.0.0) 适配器。

- 8 名受试者，行走/慢跑/骑行，腕部 PPG + 加速度 + 陀螺仪 + 胸导 ECG
- 256 Hz，开放许可 ODC-BY
- ECG R 峰人工标注保存为 WFDB .atr 注解（参考心跳金标准）
- 下载：https://physionet.org/content/wrist/1.0.0/
"""

import os
import re

import numpy as np
import wfdb

from .base import BaseAdapter, Seg

LOCAL_DIR = os.path.join("data", "raw", "wrist")


class WristAdapter(BaseAdapter):
    source = "wrist"
    license = "ODC-BY-1.0"
    fs = 256.0
    description = "Wrist PPG During Exercise (Jarchi & Casson 2017)"

    def __init__(self, root=None):
        self.root = root or LOCAL_DIR

    def load(self):
        rec_names = []
        for f in os.listdir(self.root):
            if f.endswith(".hea"):
                rec_names.append(f[:-4])
        rec_names = sorted(rec_names)
        if not rec_names:
            raise FileNotFoundError(
                f"no WRIST records in {self.root}; download from "
                "https://physionet.org/content/wrist/1.0.0/")

        segs = []
        for name in rec_names:
            rec = wfdb.rdrecord(os.path.join(self.root, name))
            # 通道查找：PPG（第一个非 ECG/运动通道按说明是 PPG）
            chans = rec.sig_name
            # 记录中通道顺序：chest_ecg, wrist_ppg, wrist_gyro_x/y/z,
            # wrist_low_noise_accelerometer_x/y/z, wrist_wide_range_accelerometer_x/y/z, ...
            ppg_idx = None
            ecg_idx = None
            acc_idx = []
            for i, c in enumerate(chans):
                c = (c or "").lower()
                if "ecg" in c:
                    ecg_idx = i
                elif "ppg" in c:
                    ppg_idx = i
                elif c.startswith("wrist_low_noise_accelerometer"):
                    acc_idx.append(i)
            if ppg_idx is None:
                ppg_idx = 0
            ppg = rec.p_signal[:, ppg_idx]

            ref = None
            try:
                ann = wfdb.rdann(os.path.join(self.root, name), "atr")
                ref = ann.sample.astype(np.int64)
            except Exception:
                ref = np.array([], dtype=np.int64)

            acc = None
            if len(acc_idx) >= 3:
                acc = np.stack([rec.p_signal[:, i] for i in acc_idx], axis=1)

            m = re.match(r"^s(\d+)_(walk|run|.*bike)", name)
            pid = m.group(1) if m else name
            activity = m.group(2) if m else "unknown"
            segs.append(Seg(
                name=name, fs=self.fs, ppg=ppg.astype(np.float64),
                ref_beats=ref, acc=acc,
                patient_id=f"wrist_s{pid}", label=activity,
            ))
        return segs, {
            "source": self.source, "license": self.license,
            "fs": self.fs, "n_segments": len(segs),
            "description": self.description,
        }
