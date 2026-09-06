#!/usr/bin/env python3
"""评估 TianshangPulse PPG 峰值检测器在外部验证集上的性能。

加载 scripts/prepare_dataset.py 生成的 external.npz（含 ref_beats），
对每个窗口用 scipy find_peaks(distance=0.4s@100Hz，与训练流水线一致)
检测 PPG 峰，与参考心跳（ECG R 波 / 官方标注）在 ±150ms 容差窗内对齐，
输出按窗口的 F1 / 灵敏度 / 精确率 与时间误差统计。

用法：
    python scripts/evaluate_beat_detection.py --npz data/processed/wrist/external.npz
    python scripts/evaluate_beat_detection.py --npz ... --window-sec 4 --tol-ms 150
"""

import argparse
import json
import os

import numpy as np
from scipy.signal import find_peaks

TARGET_FS = 100.0


def detect_peaks(window, fs=TARGET_FS, min_dist_s=0.4):
    peaks, _ = find_peaks(window, distance=int(fs * min_dist_s))
    return peaks.astype(np.int64)


def align(peaks, refs, tol_samples):
    """峰值与参考点一一匹配（每个参考最多匹配一个峰，贪心最近）。"""
    if refs.size == 0:
        return 0, len(peaks), 0, np.array([])
    refs = np.sort(refs)
    peaks = np.sort(peaks)
    matched = np.zeros(len(peaks), dtype=bool)
    errs = []
    for r in refs:
        cand = np.where(np.abs(peaks - r) <= tol_samples)[0]
        cand = cand[~matched[cand]]
        if cand.size:
            j = cand[np.argmin(np.abs(peaks[cand] - r))]
            matched[j] = True
            errs.append(peaks[j] - r)
    tp = int(matched.sum())
    fp = len(peaks) - tp
    fn = refs.size - tp
    return tp, fp, fn, np.asarray(errs, dtype=np.float64) if errs else np.array([])


def evaluate_window(win, refs, tol_ms, fs=TARGET_FS):
    tol = int(round(tol_ms / 1000.0 * fs))
    peaks = detect_peaks(win, fs=fs)
    tp, fp, fn, errs = align(peaks, refs, tol)
    return {"peaks": len(peaks), "tp": tp, "fp": fp, "fn": fn, "errs": errs}


def summarize(results, group_key=None):
    tot = {"peaks": 0, "tp": 0, "fp": 0, "fn": 0}
    errs_all = []
    for r in results:
        for k in tot:
            tot[k] += r[k]
        errs_all.append(r["errs"])
    errs = np.concatenate(errs_all) if errs_all else np.array([])
    tp, fp, fn = tot["tp"], tot["fp"], tot["fn"]
    precision = tp / (tp + fp) if (tp + fp) else 0.0
    sensitivity = tp / (tp + fn) if (tp + fn) else 0.0
    f1 = 2 * precision * sensitivity / (precision + sensitivity) if (precision + sensitivity) else 0.0
    out = {
        "windows": len(results),
        "detected_peaks": tot["peaks"],
        "ref_beats": tp + fn,
        "tp": tp, "fp": fp, "fn": fn,
        "precision": round(precision, 4),
        "sensitivity": round(sensitivity, 4),
        "f1": round(f1, 4),
    }
    if errs.size:
        out["p_err_mean_ms"] = round(float(np.mean(errs)) * 1000 / TARGET_FS, 2)
        out["p_err_std_ms"] = round(float(np.std(errs)) * 1000 / TARGET_FS, 2)
        out["|err|_mean_ms"] = round(float(np.mean(np.abs(errs))) * 1000 / TARGET_FS, 2)
    return out


def main():
    parser = argparse.ArgumentParser(description="Evaluate PPG beat detector on external data")
    parser.add_argument("--npz", required=True, help="path to external.npz")
    parser.add_argument("--tol-ms", type=float, default=150.0)
    parser.add_argument("--group-key", default="activity",
                        help="按 patient_id 前缀或 label 分组；默认 activity(从patient_id解析)")
    args = parser.parse_args()

    d = np.load(args.npz, allow_pickle=True)
    wins = d["windows"]
    pids = d["patient_id"]
    beats = d["ref_beats"]
    labels = d["labels"]
    activities = d["activity"] if "activity" in d.files else np.full(len(wins), "?", dtype=object)

    per_win = []
    for i in range(len(wins)):
        r = evaluate_window(wins[i, :, 0], np.asarray(beats[i]), args.tol_ms)
        per_win.append(r)

    groups = {}
    for i, pid in enumerate(pids):
        if args.group_key == "activity":
            key = "activity:" + str(activities[i])
        elif args.group_key == "patient":
            key = "patient:" + pid
        else:
            key = "label:" + str(int(labels[i]))
        groups.setdefault(key, []).append(per_win[i])

    report = {"npz": args.npz, "tol_ms": args.tol_ms, "fs": TARGET_FS}
    report["overall"] = summarize(per_win)
    report["groups"] = {k: summarize(v) for k, v in sorted(groups.items())}

    out_path = os.path.splitext(args.npz)[0] + "_beat_report.json"
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    o = report["overall"]
    print(f"\n=== BEAT DETECTION on {os.path.basename(args.npz)} (tol={args.tol_ms}ms) ===")
    print(f"windows={o['windows']}  ref_beats={o['ref_beats']}  detected={o['detected_peaks']}")
    print(f"F1={o['f1']:.4f}  precision={o['precision']:.4f}  sensitivity={o['sensitivity']:.4f}")
    if "|err|_mean_ms" in o:
        print(f"|err| mean={o['|err|_mean_ms']}ms  P-err mean={o['p_err_mean_ms']}ms±{o['p_err_std_ms']}ms")
    print("-- by group --")
    for k, v in report["groups"].items():
        print(f"  {k:28s} F1={v['f1']:.4f}  n={v['windows']}")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
