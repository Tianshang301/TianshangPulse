"""PPG-DaLiA 数据集适配器（PPG 达利娅，DAliA）。

- 15 名受试者，日常活动采集（坐、行走、爬楼、自行车、汽车、乒乓球、跑步），
  PPG(64 Hz) + 加速度计 + 陀螺仪 + ECG 参考，官方 .pkl 打包（daily/A 与 daily/B）
- 无 AF 节律标签（全部为窦性），用途：真实非 AF 运动噪声分布、信号质量门槛
- 许可：PPG-DaLiA 论文（Reiss et al. 2019）数据，供研究使用

下载：https://archive.ics.uci.edu/ml/datasets/PPG-DaLiA
官方 pkl 结构：dict，键含 'signal'/'label'/'activity' 等（见 ppg-beats 文档说明）
"""

import os
import pickle

import numpy as np

from .base import BaseAdapter, Seg

LOCAL_DIR = os.path.join("data", "raw", "ppg_dalia")


class PpgDaliaAdapter(BaseAdapter):
    source = "ppg_dalia"
    license = "研究使用（Reiss 2019）"
    fs = 64.0
    description = "PPG-DaLiA (Reiss et al. 2019)"

    def __init__(self, root=None):
        self.root = root or LOCAL_DIR

    def load(self):
        pkls = sorted(
            f for f in os.listdir(self.root)
            if f.endswith(".pkl")
        )
        if not pkls:
            raise FileNotFoundError(
                f"no PPG-DaLiA pkl in {self.root}；从 UCI 下载并解包到该目录")
        segs = []
        for fn in pkls:
            with open(os.path.join(self.root, fn), "rb") as f:
                data = pickle.load(f, encoding="latin1")
            pid = fn.split("_")[0] if "_" in fn else fn[:-4]
            ppg = np.asarray(data["signal"]["wrist"]["PPG"]).flatten()
            segs.append(Seg(
                name=fn, fs=self.fs, ppg=ppg.astype(np.float64),
                patient_id=f"ppg_dalia_{pid}", label="non_af",
            ))
        if not segs:
            raise FileNotFoundError("PPG-DaLiA 解析失败：pkl 内缺 signal.wrist.PPG")
        return segs, {
            "source": self.source, "license": self.license,
            "fs": self.fs, "n_segments": len(segs),
            "description": self.description,
        }
