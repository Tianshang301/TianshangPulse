#!/usr/bin/env python3
"""TianshangPulse 外部验证数据集通用准备器。

将 scripts/datasets/* 适配器解析出的原始波形段，统一转换为训练流水线同构
npz（windows @100Hz, 4s 非重叠 / 或滑窗），供 evaluate_* 使用。

统一约定（与 scripts/train_af_model.py / prepare_af_dataset.py 一致）：
    - 带通 0.5-8Hz 四阶 Butterworth，**按源采样率设计**（隐形参数失配防护）
    - 滑窗：window_sec 默认 4s，step_sec 默认 4s（非重叠）
    - 重采样至 100Hz，逐窗 Z-score + clip ±3
    - 输出 npz：windows(N,400,1) / labels / patient_id / ref_beats(N 变长，可选)

用法：
    python scripts/prepare_dataset.py --source wrist \\
        --window-sec 4 --step-sec 4 --out-dir data/processed/wrist
"""

import argparse
import json
import os

import numpy as np

from datasets.base import (bandpass, resample_beats, resample_ppg, zscore_clip)
from datasets.bidmc import BidmcAdapter
from datasets.capnobase import CapnoBaseAdapter
from datasets.mimic_iii_ext_ppg import MimicExtPpgAdapter
from datasets.ppg_dalia import PpgDaliaAdapter
from datasets.wrist import WristAdapter

ADAPTERS = {
    "wrist": WristAdapter,
    "bidmc": BidmcAdapter,
    "capnobase": CapnoBaseAdapter,
    "ppg_dalia": PpgDaliaAdapter,
    "mimic_iii_ext_ppg": MimicExtPpgAdapter,
}

TARGET_FS = 100.0


def interpolate_nan(sig):
    """线性插值稀疏 NaN（与 scripts/prepare_af_dataset.py 一致）。"""
    s = np.asarray(sig, dtype=np.float64)
    if not np.isnan(s).any():
        return s
    import pandas as pd
    out = pd.Series(s).interpolate(method="linear").bfill().ffill().to_numpy()
    if np.isnan(out).any():
        out = np.nan_to_num(out, nan=0.0)
    return out


def motion_energy(acc, fs, lo_hz=0.5, window_sec_energy=0.5):
    """每窗 IMU 运动能量：三轴加速矢量经 0.5Hz 高通后的方差。

    反映腕部运动强度（重力项被高通去除）。fs 为 acc 采样率。
    """
    if acc is None or acc.shape[0] < int(fs * 2):
        return 0.0
    a = np.asarray(acc, dtype=np.float64)
    if a.ndim == 1:
        a = a[:, None]
    mag = np.linalg.norm(a, axis=1)              # 1-d
    mag = interpolate_nan(mag)
    from scipy.signal import butter, filtfilt
    nyq = fs / 2.0
    b, aa = butter(2, lo_hz / nyq, btype="high")
    hp = filtfilt(b, aa, mag)
    return float(np.var(hp))


def build_windows(seg, window_sec, step_sec, lo=0.5, hi=8.0, order=4,
                  min_peaks=3):
    """把单个 Seg 切成 (N,400,1) 窗 + 参考峰 + 每窗运动能量。"""
    fs = seg.fs
    sig = interpolate_nan(seg.ppg)
    sig = bandpass(sig, fs=fs, lo=lo, hi=hi, order=order)

    src_win = int(round(window_sec * fs))
    src_step = max(1, int(round(step_sec * fs)))
    dst_len = int(round(window_sec * TARGET_FS))  # 400

    wins, beats, energies = [], [], []
    n = len(sig)
    for start in range(0, n - src_win + 1, src_step):
        w = resample_ppg(sig[start:start + src_win], fs, TARGET_FS)
        wins.append(zscore_clip(w))
        if seg.ref_beats.size:
            sel = seg.ref_beats[(seg.ref_beats >= start) &
                                (seg.ref_beats < start + src_win)]
            if sel.size:
                beats.append(resample_beats(sel - start, fs, TARGET_FS))
            else:
                beats.append(np.array([], dtype=np.int64))
        else:
            beats.append(np.array([], dtype=np.int64))
        if seg.acc is not None:
            acc_sel = seg.acc[start:start + src_win]
            energies.append(motion_energy(acc_sel, fs))
        else:
            energies.append(0.0)

    if not wins:
        return None
    return (np.asarray(wins, dtype=np.float32)[:, :, None],
            beats, np.asarray(energies, dtype=np.float32))


def main():
    parser = argparse.ArgumentParser(description="Prepare external validation datasets")
    parser.add_argument("--source", required=True, choices=sorted(ADAPTERS))
    parser.add_argument("--window-sec", type=float, default=4.0)
    parser.add_argument("--step-sec", type=float, default=4.0)
    parser.add_argument("--out-dir", default=None)
    parser.add_argument("--raw-root", default=None)
    parser.add_argument("--strat-folds", type=int, nargs="+", default=None,
                        help="MIMIC-Ext: 仅取指定 strat_fold（0-9）")
    parser.add_argument("--no-sqi", action="store_true",
                        help="MIMIC-Ext: 关闭 SQI 过滤")
    args = parser.parse_args()

    if args.out_dir is None:
        args.out_dir = os.path.join("data", "processed", args.source)
    os.makedirs(args.out_dir, exist_ok=True)

    cls = ADAPTERS[args.source]
    kw = {}
    if args.raw_root:
        kw["root"] = args.raw_root
    adapter = cls(**kw)

    load_kw = {}
    if args.source == "mimic_iii_ext_ppg":
        load_kw = {"sqi_filter": not args.no_sqi, "strat_folds": args.strat_folds}

    segs, meta = adapter.load(**load_kw)

    all_w, all_y, all_pid, all_beats, all_activity, all_energy = [], [], [], [], [], []
    for seg in segs:
        out = build_windows(seg, args.window_sec, args.step_sec)
        if out is None:
            continue
        w, b, energy = out
        n_w = w.shape[0]
        all_w.append(w)
        all_y.append(np.full(n_w, 1 if seg.label == "af" else 0, dtype=np.int8))
        all_pid.extend([seg.patient_id] * n_w)
        all_beats.extend(b)
        all_activity.extend([seg.label] * n_w)
        all_energy.extend(energy.tolist())

    X = np.concatenate(all_w, axis=0)
    y = np.concatenate(all_y, axis=0)
    pid = np.asarray(all_pid)
    beats = np.asarray(all_beats, dtype=object)
    activity = np.asarray(all_activity)
    motion_energy = np.asarray(all_energy, dtype=np.float32)

    np.savez_compressed(
        os.path.join(args.out_dir, "external.npz"),
        windows=X, labels=y, patient_id=pid, ref_beats=beats,
        activity=activity, motion_energy=motion_energy,
    )

    report = {
        **meta,
        "target_fs": TARGET_FS,
        "window_sec": args.window_sec, "step_sec": args.step_sec,
        "n_windows": int(len(y)),
        "n_af": int((y == 1).sum()), "n_non_af": int((y == 0).sum()),
        "n_patients": int(len(set(pid))),
        "has_motion_energy": bool(motion_energy.any()),
        "out_file": os.path.join(args.out_dir, "external.npz"),
    }
    rp = os.path.join(args.out_dir, "meta.json")
    with open(rp, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print(f"=== PREPARE {args.source} ===")
    print(f"segments loaded = {len(segs)}")
    print(f"windows         = {report['n_windows']}  (AF={report['n_af']}, non-AF={report['n_non_af']})")
    print(f"patients        = {report['n_patients']}")
    print(f"shape           = {X.shape}")
    print(f"wrote           = {report['out_file']}")


if __name__ == "__main__":
    main()
