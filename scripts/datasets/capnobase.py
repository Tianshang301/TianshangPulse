"""CapnoBase (Basel) 数据集适配器。

- 42 名手术/重症患者，约 1 分钟 PPG，300 Hz，含 ECG 与二氧化碳图，
  官方提供手工逐拍标注（呼吸 + 可选心跳）
- 需在 capnobase.org 注册申请后下载（非直链），本适配器假定解包后的
  目录为 `data/raw/capnobase/<record>/`，每段含 raw 信号与标注。

参考结构（capnobase 官方打包）：
    recordX/raw_signal/ppg.mat | ecg.mat | co2.mat
    recordX/reference/manual 心跳标注
由于官网打包格式历次有变，此处按常见 mat 布局解析；无法解析时给出明确报错。
"""

import os

import numpy as np

from .base import BaseAdapter, Seg

LOCAL_DIR = os.path.join("data", "raw", "capnobase")


class CapnoBaseAdapter(BaseAdapter):
    source = "capnobase"
    license = "capnobase 注册协议（学术使用）"
    fs = 300.0
    description = "CapnoBase dataset (Karlen et al. 2014)"

    def __init__(self, root=None):
        self.root = root or LOCAL_DIR

    def load(self):
        subdirs = sorted(
            d for d in os.listdir(self.root)
            if os.path.isdir(os.path.join(self.root, d))
        )
        if not subdirs:
            raise FileNotFoundError(
                f"no CapnoBase subjects in {self.root}；数据需在 capnobase.org "
                "注册申请下载，官方为非直链打包")
        segs = []
        for d in subdirs:
            folder = os.path.join(self.root, d)
            # 尝试常见布局：raw_signal/ppg.mat 或直接 ppg.mat
            ppg_path = os.path.join(folder, "ppg.mat")
            if not os.path.isfile(ppg_path):
                ppg_path = os.path.join(folder, "raw_signal", "ppg.mat")
            if not os.path.isfile(ppg_path):
                continue
            try:
                from scipy.io import loadmat
                ppg = np.asarray(loadmat(ppg_path)["ppg"]).flatten()
            except Exception as e:
                raise RuntimeError(f"parse {ppg_path} failed: {e}")
            segs.append(Seg(
                name=d, fs=self.fs, ppg=ppg.astype(np.float64),
                patient_id=f"capnobase_{d}", label="non_af",
            ))
        if not segs:
            raise FileNotFoundError(
                f"CapnoBase 解析失败：未找到 ppg.mat（预期布局 raw_signal/ppg.mat）")
        return segs, {
            "source": self.source, "license": self.license,
            "fs": self.fs, "n_segments": len(segs),
            "note": "需人工下载；ref_beats 未提供则留空",
            "description": self.description,
        }
