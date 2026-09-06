"""MIMIC-III-Ext-PPG 适配器（凭据访问，代码就绪 / 数据待下载）。

- 6,189 名重症患者，约 6.4M 个 30s 段（AF 1,132 患者 / 597,769 段），125 Hz，WFDB
- 自带 SQI（vector_10s_pleth_sqi）与 strat_fold（10 折患者分层）、metadata.csv
- 许可：PhysioNet Credentialed Health Data License 1.5.0（需 DUA + CITI）
- 本适配器仅实现代码路径：SQI 过滤 + 标签映射 + strat_fold 复用；数据需人工凭据下载

标签映射（event_rhythm）：SR->NON_AF, AF->AF；AFLT 等非目标节律 -> EXCLUDE。
SQI 过滤：三窗口 vector_10s_pleth_sqi 全部为 1（高质量）或 0（可用）才保留。

下载（凭据）：
    wget -r -N -c -np https://physionet.org/files/mimic-iii-ext-ppg/1.1.0/
"""

import os

import numpy as np
import pandas as pd

from .base import BaseAdapter, Seg

LOCAL_DIR = os.path.join("data", "raw", "mimic_iii_ext_ppg")
VALID_SQI = {1, 0}


class MimicExtPpgAdapter(BaseAdapter):
    source = "mimic_iii_ext_ppg"
    license = "PhysioNet Credentialed Health Data License 1.5.0 (DUA+CITI)"
    fs = 125.0
    description = "MIMIC-III-Ext-PPG (Moulaeifard 2026)"

    def __init__(self, root=None):
        self.root = root or LOCAL_DIR

    def _map_label(self, rhythm):
        r = (rhythm or "").strip().upper()
        if r == "SR":
            return "non_af"
        if r == "AF":
            return "af"
        return "exclude"

    def load(self, sqi_filter=True, strat_folds=None):
        meta_path = os.path.join(self.root, "metadata.csv")
        if not os.path.isfile(meta_path):
            raise FileNotFoundError(
                f"{meta_path} 缺失：该数据集需 PhysioNet 凭据（DUA+CITI）下载，"
                "并受地区政策限制；请人工获取后重试")
        df = pd.read_csv(meta_path)

        if strat_folds is not None:
            df = df[df["strat_fold"].isin(strat_folds)]

        labels = df["event_rhythm"].map(self._map_label)
        df = df[labels != "exclude"]
        df = df[labels[labels != "exclude"].index]

        if sqi_filter and "vector_10s_pleth_sqi" in df.columns:
            # 字符串数组 -> 数值矩阵；要求三窗口均有效
            import ast
            sqi = df["vector_10s_pleth_sqi"].apply(
                lambda s: np.asarray(ast.literal_eval(s), dtype=int)
            )
            ok = sqi.apply(lambda v: set(v.tolist()).issubset(VALID_SQI))
            df = df[ok]

        if df.empty:
            raise RuntimeError("MIMIC-Ext: 无通过 SQI/标签过滤的段（检查 strat_fold）")

        import wfdb
        segs = []
        for _, row in df.iterrows():
            rel = os.path.join(self.root, row["folder_path"], row["signal_file_name"])
            if not (os.path.isfile(rel + ".hea") and os.path.isfile(rel + ".dat")):
                continue
            rec = wfdb.rdrecord(rel)
            ppg_idx = next((i for i, c in enumerate(rec.sig_name)
                            if (c or "").lower() in ("pleth", "ppg")), 0)
            segs.append(Seg(
                name=row["signal_file_name"], fs=self.fs,
                ppg=rec.p_signal[:, ppg_idx].astype(np.float64),
                patient_id=f"p{int(row['subject_id']):06d}",
                label=str(row["event_rhythm"]).upper(),
            ))
        return segs, {
            "source": self.source, "license": self.license,
            "fs": self.fs, "n_segments": len(segs),
            "sqi_filter": sqi_filter, "strat_folds": strat_folds,
            "description": self.description,
        }
