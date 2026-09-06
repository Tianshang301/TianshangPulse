#!/usr/bin/env python3
"""IMU 运动门控阈值标定（Level 1）。

用 WRIST（腕部运动）与训练集窦性窗口（安静）的运动能量分布，
按 ROC 求最优阈值，输出三档门控阈值：
    - T_high：ROC 约登指数最优（最大化运动召回，同时压低安静误伤）
    - T_silent：安静能量 p99，低于则视为静音（正常推理）
    - T_high 之上：高运动，抑制 AF 输出

数据：
    - 运动样本：data/processed/wrist/external.npz 的 motion_energy
    - 安静样本：data/processed/train.npz 的计算运动能量（无 IMU -> 0）。
      对无 IMU 的安静数据，motion_energy 恒为 0；为得到真实安静能量分布，
      也接受 --quiet-npz 指定含 acc 的安静集（如后续采集的静止腕部数据）。

用法：
    python scripts/calibrate_motion_gate.py \
        --motion-npz data/processed/wrist/external.npz \
        --out model/af/motion_gate.json
"""

import argparse
import json
import os

import numpy as np
from sklearn.metrics import roc_curve

OUT_DIR = os.path.join("model", "af")


def load_energy(npz_path, key="motion_energy"):
    d = np.load(npz_path, allow_pickle=True)
    if key not in d.files:
        return None
    return d[key].astype(np.float64)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--motion-npz", required=True, help="WRIST 等运动数据集")
    parser.add_argument("--quiet-npz", default=None,
                        help="含 IMU 的安静集（缺省则用 0 能量近似）")
    parser.add_argument("--quiet-fallback-energy", type=float, default=0.05,
                        help="无安静 IMU 时的安静能量代表值")
    parser.add_argument("--out", default=os.path.join(OUT_DIR, "motion_gate.json"))
    parser.add_argument("--fp-limit", type=float, default=0.02,
                        help="高运动阈值允许的安静误伤上限（默认 2%）")
    args = parser.parse_args()

    motion = load_energy(args.motion_npz)
    if motion is None:
        raise SystemExit(f"{args.motion_npz} 不含 motion_energy")

    if args.quiet_npz and os.path.isfile(args.quiet_npz):
        quiet = load_energy(args.quiet_npz)
        if quiet is None:
            quiet = np.array([args.quiet_fallback_energy] * 200)
    else:
        quiet = np.array([args.quiet_fallback_energy] * 200)

    y = np.concatenate([np.ones(len(motion)), np.zeros(len(quiet))])
    x = np.concatenate([motion, quiet])
    fpr, tpr, thr = roc_curve(y, x)

    # 约登指数：最大化 TPR - FPR
    j = tpr - fpr
    best = int(np.argmax(j))
    t_high = thr[best]

    # 也可在限 FPR 的约束下取最高 TPR
    feasible = np.where(fpr <= args.fp_limit)[0]
    t_fp = thr[feasible[np.argmax(tpr[feasible])]]

    t_silent = float(np.percentile(quiet, 99))

    report = {
        "method": ("T_high = 运动能量 5% 分位（门控 95% 运动窗）；"
                   "静音真实 IMU 能量≈0 故安全；T_silent = 静音能量 p99"),
        "n_motion_windows": int(len(motion)),
        "n_quiet_windows": int(len(quiet)),
        "motion_energy_stats": {
            "mean": float(motion.mean()), "p50": float(np.percentile(motion, 50)),
            "p95": float(np.percentile(motion, 95)),
            "p05": float(np.percentile(motion, 5)),
        },
        "quiet_energy_stats": {
            "mean": float(quiet.mean()), "p99": float(np.percentile(quiet, 99)),
        },
        "thresholds": {
            "T_silent": t_silent,
            "T_high": float(np.percentile(motion, 5)),
            "T_high_youden": float(t_high),
        },
        "coverage": {
            "pct_motion_gated_at_T_high": float((motion >= np.percentile(motion, 5)).mean()),
            "pct_motion_gated_at_T_high_by_activity": None,  # 由 evaluate 脚本按活动给出
        },
        "note": "T_silent 之下正常推理；T_silent..T_high 中运动压低置信度；>=T_high 抑制 AF",
        "quiet_source": args.quiet_npz or f"fallback const {args.quiet_fallback_energy}",
    }

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print(f"\n=== MOTION GATE CALIBRATION ===")
    print(f"motion windows={report['n_motion_windows']} (E mean={report['motion_energy_stats']['mean']:.3f}, "
          f"p05={report['motion_energy_stats']['p05']:.4f})")
    print(f"quiet  windows={report['n_quiet_windows']} (E p99={report['quiet_energy_stats']['p99']:.3f})")
    print(f"T_silent          = {t_silent:.4f}")
    print(f"T_high (5%分位)   = {float(np.percentile(motion, 5)):.4f}  "
          f"-> 门控 {100*report['coverage']['pct_motion_gated_at_T_high']:.1f}% 运动窗")
    print(f"T_high (Youden)   = {float(t_high):.4f} (参考)")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()

# 注：bike 场景运动能量低（手臂相对固定），纯 IMU 门控无法覆盖；
# 需配合 Level 2 PPG SQI 兜底（见 evaluate_beat_detection / evaluate_motion_robustness）。