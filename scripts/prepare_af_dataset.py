#!/usr/bin/env python3
"""Prepare MIMIC PERform AF dataset for LSTM AF-detection training.

Reads the raw CSV export (record 10.5281/zenodo.15906524), applies PPG
preprocessing (bandpass + window Z-score + clip), slides 1 s windows,
resamples to 100 points to align with the 100 Hz firmware sampling rate,
and splits by patient (GroupShuffleSplit) so no patient overlaps
train/validation.

Outputs (not tracked by git):
  data/processed/train.npz     windows (N,100,1) / labels / patient_id
  data/processed/val.npz       same schema
  data/processed/mimic_perform_af_meta.json  split + provenance

Usage:
    python scripts/prepare_af_dataset.py
    python scripts/prepare_af_dataset.py --window-sec 1 --step-sec 0.5
"""

import argparse
import json
import os

import numpy as np
import pandas as pd
from scipy.signal import butter, filtfilt
from sklearn.model_selection import GroupShuffleSplit

RAW_ROOT = os.path.join("data", "raw", "mimic_perform_af")
OUT_DIR = os.path.join("data", "processed")
METADATA = {
    "source": "MIMIC PERform AF Dataset (record 10.5281/zenodo.15906524)",
    "license": "ODbL-1.0",
    "signal_rate_hz": 125,
    "target_rate_hz": 100,
}


def bandpass(sig, fs=125.0, lo=0.5, hi=8.0, order=4):
    nyq = fs / 2.0
    b, a = butter(order, [lo / nyq, hi / nyq], btype="band")
    return filtfilt(b, a, sig)


def resample_window(win, src_len, dst_len):
    x_src = np.linspace(0, src_len - 1, src_len)
    x_dst = np.linspace(0, src_len - 1, dst_len)
    return np.interp(x_dst, x_src, win)


def preprocess_window(ppg_win):
    ppg_win = np.asarray(ppg_win, dtype=np.float32)
    mean = np.mean(ppg_win)
    std = np.std(ppg_win)
    if std < 1e-8:
        std = 1e-8
    ppg_win = (ppg_win - mean) / std
    return np.clip(ppg_win, -3.0, 3.0)


def load_subject(folder, prefix, window_samples, step_samples, target_rate=100.0,
                 max_nan_frac=0.25):
    """Return (ppg_windows, patient_ids) for one subject.

    Sparse NaNs are linearly interpolated; subjects whose PPG is mostly
    missing (> max_nan_frac) are skipped entirely.
    Windows are resampled to target_rate*seconds points (default 100 Hz).
    """
    data_file = os.path.join(folder, f"{prefix}_data.csv")
    df = pd.read_csv(data_file)

    ppg = df["PPG"].to_numpy(dtype=np.float64)
    nan_frac = np.isnan(ppg).mean()
    if nan_frac > max_nan_frac:
        print(f"    {prefix}: skipped, {nan_frac:.1%} PPG NaN")
        return []

    series = pd.Series(ppg)
    ppg = series.interpolate(method="linear").bfill().ffill().to_numpy()
    ppg = bandpass(ppg)

    target_samples = int(round(target_rate * window_samples / METADATA["signal_rate_hz"]))
    windows = []
    n = len(ppg)
    for start in range(0, n - window_samples + 1, step_samples):
        seg = ppg[start:start + window_samples]
        seg = resample_window(seg, window_samples, target_samples)
        seg = preprocess_window(seg)
        windows.append(seg)

    return windows


def main():
    parser = argparse.ArgumentParser(description="Prepare MIMIC PERform AF dataset")
    parser.add_argument("--raw-root", default=RAW_ROOT)
    parser.add_argument("--out-dir", default=OUT_DIR)
    parser.add_argument("--window-sec", type=float, default=4.0)
    parser.add_argument("--step-sec", type=float, default=4.0)
    parser.add_argument("--val-ratio", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    sample_rate = METADATA["signal_rate_hz"]
    window_samples = int(round(args.window_sec * sample_rate))
    step_samples = max(1, int(round(args.step_sec * sample_rate)))
    target_rate = METADATA.get("target_rate_hz", 100.0)
    target_samples = int(round(target_rate * args.window_sec))

    groups = [
        ("af", os.path.join(args.raw_root, "af", "mimic_perform_af_csv"), "mimic_perform_af", 1),
        ("non_af", os.path.join(args.raw_root, "non_af", "mimic_perform_non_af_csv"),
         "mimic_perform_non_af", 0),
    ]

    all_windows = []
    all_labels = []
    all_patients = []

    for label_name, folder, prefix, label in groups:
        print(f"[prepare] scanning {label_name}: {folder}")
        subjects = sorted(
            f for f in os.listdir(folder)
            if f.endswith("_data.csv") and f.startswith(prefix + "_")
        )
        for subj_file in subjects:
            subj_prefix = subj_file[: -len("_data.csv")]
            pat_id = subj_prefix.rsplit("_", 1)[-1]
            wins = load_subject(folder, subj_prefix, window_samples, step_samples,
                                target_rate=target_rate)
            all_windows.extend(wins)
            all_labels.extend([label] * len(wins))
            all_patients.extend([f"{label_name}_{pat_id}"] * len(wins))
            print(f"    {subj_prefix}: {len(wins)} windows")

    X = np.asarray(all_windows, dtype=np.float32)[:, :, None]  # (N, target_samples, 1)
    y = np.asarray(all_labels, dtype=np.int8)
    patients = np.asarray(all_patients)

    assert not np.isnan(X).any(), "NaN in windows"
    assert set(np.unique(y)) <= {0, 1}, "labels must be 0/1"

    gss = GroupShuffleSplit(n_splits=1, test_size=args.val_ratio, random_state=args.seed)
    train_idx, val_idx = next(gss.split(X, y, groups=patients))

    train_patients = set(patients[train_idx])
    val_patients = set(patients[val_idx])
    assert not (train_patients & val_patients), "patient overlap train/val"

    os.makedirs(args.out_dir, exist_ok=True)
    np.savez_compressed(os.path.join(args.out_dir, "train.npz"),
                        windows=X[train_idx], labels=y[train_idx],
                        patient_id=patients[train_idx])
    np.savez_compressed(os.path.join(args.out_dir, "val.npz"),
                        windows=X[val_idx], labels=y[val_idx],
                        patient_id=patients[val_idx])

    meta = {
        **METADATA,
        "window_sec": args.window_sec,
        "step_sec": args.step_sec,
        "window_samples_source": window_samples,
        "target_samples": target_samples,
        "total_windows": int(len(y)),
        "train_windows": int(len(train_idx)),
        "val_windows": int(len(val_idx)),
        "train_patients": len(train_patients),
        "val_patients": len(val_patients),
        "seed": args.seed,
        "note": f"single channel PPG ({target_samples},1) @ {target_rate:.0f}Hz; ECG/resp not used for model input",
    }
    meta_path = os.path.join(args.out_dir, "mimic_perform_af_meta.json")
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2, ensure_ascii=False)

    print("\n=== SUMMARY ===")
    print(f"total windows : {len(y)}  (AF={int((y==1).sum())}, non-AF={int((y==0).sum())})")
    print(f"train windows : {len(train_idx)}  patients={len(train_patients)}")
    print(f"val windows   : {len(val_idx)}  patients={len(val_patients)}")
    print(f"shape         : {X.shape}")
    print(f"wrote         : {args.out_dir}")


if __name__ == "__main__":
    main()