#!/usr/bin/env python3
"""TianshangPulse 双端（训练端 Python vs 固件 C）数值对齐验证。

验证目标：固件处理链（ppg_preprocess_zscore_clip -> signal_gate_ppg_sqi ->
af_features_extract）与 numpy 参考实现（与 C 逐行同构）在**同一 raw 输入**下
输出一致（A4 修复后的 train/serve 对齐保障）。

流程：
  1. 从 data/processed/*.npz 读取已归一化（z-score+clip）的存档窗；
  2. 逆归一化构造 raw 输入（raw = w*sigma + mu，模拟真实 IR 直流/幅度）；
  3. 编译并运行 C harness（编译真实的 firmware/main/sensors 源码）；
  4. numpy 参考实现从同一 raw 完整复现 z-score -> 峰值 -> 特征/SQI；
  5. 硬门：n_peaks 完全一致 + 特征/SQI 容差内；
     软报告：scipy find_peaks（训练端原始抽取器）与 C 峰集合一致率
     （量化 plateau/distance 规则的残余 skew，不 gate）。

编译器发现顺序：gcc / clang / cl / zig(pip install ziglang)。

用法：
  python scripts/parity_check.py [--npz data/processed/train.npz] [--max-windows 200]
"""

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys

import numpy as np

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW_MAIN = os.path.join(REPO_ROOT, "firmware", "main")
HARNESS = os.path.join(REPO_ROOT, "tests", "parity", "parity_harness.c")

FEATURE_NAMES = ["rri_std", "rmssd", "pnn50", "cv", "hr_proxy", "diff_std"]


def find_cc():
    """返回可用的 host C 编译器命令（list），找不到返回 None。"""
    for name in ("gcc", "clang"):
        p = shutil.which(name)
        if p:
            return [p]
    if shutil.which("cl"):
        return ["cl"]
    try:
        import ziglang  # noqa: F401
        return [sys.executable, "-m", "ziglang", "cc"]
    except ImportError:
        return None


def build_harness(cc, out_exe):
    srcs = [
        HARNESS,
        os.path.join(FW_MAIN, "sensors", "ppg_preprocess.c"),
        os.path.join(FW_MAIN, "sensors", "signal_gate.c"),
        os.path.join(FW_MAIN, "sensors", "af_features.c"),
    ]
    cmd = cc + ["-O2", "-I", FW_MAIN] + srcs + ["-o", out_exe, "-lm"]
    print("[build]", " ".join(cmd))
    subprocess.run(cmd, check=True)


def write_windows(path, wins):
    """wins: (N, L) float32 -> little-endian 二进制。"""
    with open(path, "wb") as f:
        f.write(struct.pack("<i", wins.shape[0]))
        for w in wins:
            f.write(struct.pack("<i", w.shape[0]))
            f.write(struct.pack("<%df" % w.shape[0], *w.astype(np.float32).tolist()))


# ---------- numpy 参考实现（与 C 实现逐行同构） ----------

def zscore_clip_ref(w):
    """复刻 firmware/main/sensors/ppg_preprocess.c（double 累加 + float32 输出）。"""
    x = w.astype(np.float64)
    mean = float(x.mean())
    std = float(x.std())
    if std < 1e-8:
        std = 1e-8
    out = ((x - mean) / std).astype(np.float32)
    return np.clip(out, -3.0, 3.0)


def detect_peaks_c(s, fs=100.0, max_peaks=128):
    """复刻固件 af_features.c/signal_gate.c 峰值规则：
    局部极大（>= 两侧）且幅值 > 0；distance 首峰优先（保先出现者）。"""
    dist = int(0.4 * fs)
    peaks = []
    for i in range(1, len(s) - 1):
        if len(peaks) >= max_peaks:
            break
        if s[i] >= s[i - 1] and s[i] >= s[i + 1] and s[i] > 0.0:
            if not peaks or (i - peaks[-1]) >= dist:
                peaks.append(i)
    return peaks


def features_ref(s, fs=100.0):
    """复刻固件 af_features_extract 的 6 维特征（af_features.c）。"""
    peaks = detect_peaks_c(s, fs, max_peaks=128)
    n_p = len(peaks)
    if n_p < 3:
        return n_p, [0.0] * 6
    rri = [(peaks[i + 1] - peaks[i]) / fs * 1000.0 for i in range(n_p - 1)]
    m = sum(rri) / len(rri)
    var = sum((x - m) ** 2 for x in rri) / len(rri)
    std_rri = math.sqrt(var)
    d = [rri[i + 1] - rri[i] for i in range(len(rri) - 1)]
    rmssd = math.sqrt(sum(x * x for x in d) / len(d))
    pnn50 = sum(1 for x in d if abs(x) > 50.0) / len(d)
    cv = std_rri / m if m > 0 else 0.0
    hr = n_p / (len(s) / fs)
    diff = [float(s[i + 1]) - float(s[i]) for i in range(len(s) - 1)]
    dm = sum(diff) / len(diff)
    dv = sum((x - dm) ** 2 for x in diff) / len(diff)
    return n_p, [std_rri, rmssd, pnn50, cv, hr, math.sqrt(dv)]


def sqi_ref(s, fs=100.0):
    """复刻固件 signal_gate_ppg_sqi（signal_gate.c）。"""
    peaks = detect_peaks_c(s, fs, max_peaks=64)
    if len(peaks) < 3:
        return 0.0
    amps = [float(s[p]) for p in peaks]
    mean_amp = sum(amps) / len(amps)
    var_amp = sum(a * a for a in amps) / len(amps) - mean_amp * mean_amp
    if var_amp < 0:
        var_amp = 0.0
    cv = math.sqrt(var_amp) / (mean_amp + 1e-8)
    sqi_amp_cv = 1.0 / (1.0 + cv)
    ok = sum(1 for i in range(1, len(peaks))
             if 600.0 <= (peaks[i] - peaks[i - 1]) / fs * 1000.0 <= 1200.0)
    sqi_interval = ok / (len(peaks) - 1)
    hr = 60.0 * len(peaks) / (len(s) / fs)
    sqi_hr = 1.0 if 30.0 <= hr <= 250.0 else 0.0
    return math.sqrt(sqi_amp_cv * sqi_interval * sqi_hr)

def within(got, ref, abs_tol, rel_tol):
    return abs(got - ref) <= abs_tol + rel_tol * abs(ref)


def main():
    ap = argparse.ArgumentParser(description="firmware-vs-python parity check")
    ap.add_argument("--npz", default=os.path.join(REPO_ROOT, "data", "processed", "train.npz"))
    ap.add_argument("--max-windows", type=int, default=200)
    ap.add_argument("--abs-tol", type=float, default=1e-3)
    ap.add_argument("--rel-tol", type=float, default=1e-3)
    ap.add_argument("--sqi-tol", type=float, default=1e-4)
    ap.add_argument("--data-mu", type=float, default=0.4, help="raw 逆归一化直流偏置")
    ap.add_argument("--data-sigma", type=float, default=0.25, help="raw 逆归一化幅度")
    ap.add_argument("--out-dir", default=os.path.join(REPO_ROOT, "data", "processed"))
    args = ap.parse_args()

    if not os.path.isfile(args.npz):
        raise SystemExit("npz not found: %s" % args.npz)
    cc = find_cc()
    if cc is None:
        raise SystemExit("no C compiler found (gcc/clang/cl/zig). "
                         "install: pip install ziglang")

    build_dir = os.path.join(args.out_dir, "parity_build")
    os.makedirs(build_dir, exist_ok=True)
    exe = os.path.join(build_dir, "parity_harness.exe")
    build_harness(cc, exe)

    d = np.load(args.npz)
    windows = d["windows"][:, :, 0].astype(np.float32)
    n_total = windows.shape[0]
    stride = max(1, n_total // max(1, args.max_windows))
    sel = windows[::stride][:args.max_windows]
    raw = sel * np.float32(args.data_sigma) + np.float32(args.data_mu)
    print("[data] %s: total=%d selected=%d stride=%d" %
          (args.npz, n_total, sel.shape[0], stride))

    bin_path = os.path.join(build_dir, "windows.bin")
    out_path = os.path.join(build_dir, "c_features.txt")
    write_windows(bin_path, raw)
    subprocess.run([exe, bin_path, out_path], check=True)
    with open(out_path, "r", encoding="utf-8") as f:
        c_rows = [l.split() for l in f.read().splitlines() if l.strip()]
    if len(c_rows) != sel.shape[0]:
        raise SystemExit("harness rows %d != windows %d" % (len(c_rows), sel.shape[0]))

    # ---- 硬门对比：numpy 参考 vs C 输出 ----
    feat_max_abs = [0.0] * 6
    feat_max_rel = [0.0] * 6
    sqi_max_abs = 0.0
    peaks_mismatch = []
    fails = []
    for i, w in enumerate(raw):
        norm = zscore_clip_ref(w)
        n_ref, feat_ref = features_ref(norm)
        sqi_ref_v = sqi_ref(norm)

        row = c_rows[i]
        n_c = int(row[0])
        sqi_c = float(row[1])
        feat_c = [float(x) for x in row[2:8]]

        if n_c != n_ref:
            peaks_mismatch.append((i, n_ref, n_c))
            fails.append((i, "n_peaks %d != %d" % (n_c, n_ref)))
            continue

        for j in range(6):
            diff = abs(feat_c[j] - feat_ref[j])
            rel = diff / max(abs(feat_ref[j]), 1e-12)
            feat_max_abs[j] = max(feat_max_abs[j], diff)
            feat_max_rel[j] = max(feat_max_rel[j], rel)
            if not within(feat_c[j], feat_ref[j], args.abs_tol, args.rel_tol):
                fails.append((i, "%s: C=%.6f ref=%.6f" %
                              (FEATURE_NAMES[j], feat_c[j], feat_ref[j])))

        sdiff = abs(sqi_c - sqi_ref_v)
        sqi_max_abs = max(sqi_max_abs, sdiff)
        if sdiff > args.sqi_tol:
            fails.append((i, "sqi: C=%.6f ref=%.6f" % (sqi_c, sqi_ref_v)))

    # ---- 软报告：训练端 scipy find_peaks vs C 峰集合（残余 skew，不 gate） ----
    from scipy.signal import find_peaks
    agree = 0
    feat_skew = 0
    for i in range(sel.shape[0]):
        sp, _ = find_peaks(sel[i], distance=int(100.0 * 0.4))
        cp = detect_peaks_c(zscore_clip_ref(raw[i]))
        if list(int(x) for x in sp) == [int(x) for x in cp]:
            agree += 1
        else:
            _, fr = features_ref(zscore_clip_ref(raw[i]))
            _, fs_ = _features_scipy(sel[i])
            if max(abs(a - b) / max(abs(a), 1e-12) for a, b in zip(fr, fs_)) > 0.05:
                feat_skew += 1

    report = {
        "npz": args.npz,
        "n_windows": int(sel.shape[0]),
        "abs_tol": args.abs_tol, "rel_tol": args.rel_tol, "sqi_tol": args.sqi_tol,
        "n_peaks_mismatch": len(peaks_mismatch),
        "feature_max_abs_diff": {n: v for n, v in zip(FEATURE_NAMES, feat_max_abs)},
        "feature_max_rel_diff": {n: v for n, v in zip(FEATURE_NAMES, feat_max_rel)},
        "sqi_max_abs_diff": sqi_max_abs,
        "scipy_vs_c_peak_sets_agree_pct": round(100.0 * agree / sel.shape[0], 2),
        "scipy_vs_c_feature_skew_gt5pct": feat_skew,
        "hard_gate_fails": fails[:20],
        "verdict": "PASS" if not fails else "FAIL",
    }
    rp = os.path.join(args.out_dir, "parity_report.json")
    with open(rp, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print("")
    print("=== PARITY REPORT ===")
    print("windows              : %d" % sel.shape[0])
    print("n_peaks mismatch     : %d" % len(peaks_mismatch))
    for n, v in zip(FEATURE_NAMES, feat_max_abs):
        print("  max|d| %-9s: %.2e (rel %.2e)" % (n, v, feat_max_rel[FEATURE_NAMES.index(n)]))
    print("sqi max|d|           : %.2e" % sqi_max_abs)
    print("scipy peak-set agree : %.2f%% (residual skew, informational)" %
          (100.0 * agree / sel.shape[0]))
    print("feature skew >5%%     : %d windows" % feat_skew)
    print("verdict              : %s" % report["verdict"])
    if fails:
        print("first failures:")
        for i, msg in fails[:5]:
            print("  window %d: %s" % (i, msg))
    print("report -> %s" % rp)
    return 0 if not fails else 1


def _features_scipy(w, fs=100.0):
    """训练端原始抽取器（train_af_model.py extract_features 的窗口级复刻）。"""
    from scipy.signal import find_peaks
    peaks, _ = find_peaks(w, distance=int(fs * 0.4))
    n_p = len(peaks)
    if n_p < 3:
        return n_p, [0.0] * 6
    rri = np.diff(peaks) / fs * 1000.0
    diff = np.diff(np.asarray(w, dtype=np.float64))
    return n_p, [
        float(np.std(rri)),
        float(np.sqrt(np.mean(np.diff(rri) ** 2))),
        float(np.mean(np.abs(np.diff(rri)) > 50.0)),
        float(np.std(rri) / np.mean(rri)),
        float(n_p / (len(w) / fs)),
        float(np.std(diff)),
    ]


if __name__ == "__main__":
    sys.exit(main())