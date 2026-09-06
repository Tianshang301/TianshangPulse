#!/usr/bin/env python3
"""评估现有 7 参数 LR 模型在运动伪影（WRIST）下的鲁棒性。

仅验证、不重训模型：
    1. 加载已训练 LR（model/af/af_model_lr.npz）
    2. 在 WRIST 外部数据上计算 6 维 RRI 特征（全为窦性，无 AF）
    3. 模拟 Level 1 IMU 门控 + Level 2 PPG SQI：
         - 门控命中（motion_energy >= T_high 或 SQI < 阈值）-> 输出被抑制
         - 未命中 -> 正常跑 LR
    4. 统计各活动下「AF 假阳性率」——同时报告【无门控】(原始) 与
       【门控后】(抑制窗口不计入 AF 输出)。门控后 FPR 目标 <0.10。

用法：
    python scripts/evaluate_motion_robustness.py --npz data/processed/wrist/external.npz \
        --gate --gate-json model/af/motion_gate.json \
        --sqi-threshold 0.3 --threshold 0.5
"""

import argparse
import json
import os

import numpy as np

from ppg_sqi import ppg_sqi
from train_af_model import FEATURES, extract_features

OUT_DIR = os.path.join("model", "af")


def sigmoid(z):
    return 1.0 / (1.0 + np.exp(-z))


def load_lr(path=None):
    path = path or os.path.join(OUT_DIR, "af_model_lr.npz")
    w = np.load(path)
    return w["coef"], float(w["intercept"])


def main():
    parser = argparse.ArgumentParser(description="Motion robustness of fixed LR model (WRIST)")
    parser.add_argument("--npz", required=True)
    parser.add_argument("--threshold", type=float, default=0.5)
    parser.add_argument("--lr-weights", default=None)
    parser.add_argument("--gate", action="store_true", help="启用 Level 1+2 门控")
    parser.add_argument("--gate-json", default=os.path.join(OUT_DIR, "motion_gate.json"))
    parser.add_argument("--sqi-threshold", type=float, default=0.3,
                        help="SQI 低于此值 -> 抑制（Level 2）")
    args = parser.parse_args()

    coef, intercept = load_lr(args.lr_weights)
    d = np.load(args.npz, allow_pickle=True)
    windows = d["windows"]
    activities = d["activity"] if "activity" in d.files else np.full(len(windows), "?", dtype=object)
    energy = d["motion_energy"] if "motion_energy" in d.files else None

    X = extract_features(windows)
    assert not np.isnan(X).any()

    score = X @ coef + intercept
    prob = sigmoid(score)
    pred_af_raw = (prob >= args.threshold)

    # 门控掩码：被抑制（不计入 AF 输出）的窗口
    suppressed = np.zeros(len(windows), dtype=bool)
    if args.gate:
        t_high = None
        t_silent = None
        if os.path.isfile(args.gate_json):
            with open(args.gate_json, encoding="utf-8") as fh:
                g = json.load(fh)
            t_high = g["thresholds"]["T_high"]
            t_silent = g["thresholds"]["T_silent"]
        if energy is not None and t_high is not None:
            suppressed |= (energy >= t_high)
        # Level 2 SQI
        sqi_vals = np.array([ppg_sqi(w)[0] for w in windows[:, :, 0]])
        suppressed |= (sqi_vals < args.sqi_threshold)

    # 门控后：被抑制窗口视为无 AF 输出（不计数为假阳性）
    pred_af_gated = pred_af_raw & ~suppressed

    def stats(pred, mask_used):
        groups = {}
        for a in np.unique(activities):
            m = activities == a
            n = int(m.sum())
            if n == 0:
                continue
            active = m & ~mask_used                 # 未被抑制的窗口
            active_n = int(active.sum())
            fp = int((pred & active).sum())
            groups[str(a)] = {
                "n": n,
                "suppressed": int((mask_used & m).sum()),
                "active": active_n,
                # 主动误报率：已发出 AF 中未被抑制的占比（分母=该活动全部窗）
                "emitted_fp_rate": fp / n if n else 0.0,
                # 剩余窗内的 FPR（诊断用）
                "active_fp_rate": fp / active_n if active_n else None,
                "prob_mean": float(prob[m].mean()),
            }
        active_all = int((~mask_used).sum())
        fp_all = int((pred & ~mask_used).sum())
        return {
            "n": len(pred),
            "active": active_all,
            "suppressed": int(mask_used.sum()),
            "emitted_fp_rate": fp_all / len(pred),
            "active_fp_rate": fp_all / active_all if active_all else None,
            "by_activity": groups,
        }

    # 简化：mask_used = suppressed（被抑制窗口从分母中去掉计入"已处理"）
    raw_res = stats(pred_af_raw, np.zeros(len(windows), dtype=bool))
    gated_res = stats(pred_af_gated, suppressed)

    feat_stats = {}
    for j, name in enumerate(FEATURES):
        feat_stats[name] = {
            "mean": float(X[:, j].mean()), "median": float(np.median(X[:, j])),
            "p95": float(np.percentile(X[:, j], 95)),
        }

    threshold_info = {"T_high": None, "T_silent": None}
    if args.gate and os.path.isfile(args.gate_json):
        with open(args.gate_json, encoding="utf-8") as fh:
            threshold_info = json.load(fh)["thresholds"]

    report = {
        "model": "logistic_regression (7 params, fixed, not retrained)",
        "lr_weights": args.lr_weights or os.path.join(OUT_DIR, "af_model_lr.npz"),
        "threshold": args.threshold,
        "sqi_threshold": args.sqi_threshold,
        "gating": {
            "enabled": args.gate,
            "T_high": threshold_info.get("T_high"),
            "T_silent": threshold_info.get("T_silent"),
        },
        "dataset": args.npz,
        "note": ("WRIST 全为窦性；emitted_fp_rate = 发出且未被抑制的 AF 误判 / 全部窗"
                 "（验收 <0.10）；active_fp_rate 为剩余窗内误判率（诊断用）"),
        "ungated": raw_res,
        "gated": gated_res,
        "features": feat_stats,
    }

    out_path = os.path.splitext(args.npz)[0] + "_motion_report.json"
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print(f"\n=== MOTION ROBUSTNESS ({os.path.basename(args.npz)}) ===")
    print(f"windows={report['ungated']['n']}")
    print(f"[ungated] emitted-AF-FP = {raw_res['emitted_fp_rate']:.4f} (of all windows)")
    if args.gate:
        print(f"[gated  ] emitted-AF-FP = {gated_res['emitted_fp_rate']:.4f}   "
              f"(suppressed={suppressed.sum()}/{len(suppressed)} windows)  [验收 <0.10]")
        print("-- by activity (gated) --")
        for k, v in sorted(gated_res["by_activity"].items()):
            u = raw_res["by_activity"][k]
            print(f"  {k:22s} raw FP={u['emitted_fp_rate']:.3f} -> gated={v['emitted_fp_rate']:.3f}  "
                  f"(suppressed={v['suppressed']}/{v['n']}, active={v['active']})")
    print("-- feature means (ungated) --")
    for k, v in feat_stats.items():
        print(f"  {k:10s} mean={v['mean']:.3f}  median={v['median']:.3f}")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()