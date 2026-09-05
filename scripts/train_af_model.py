#!/usr/bin/env python3
"""Train the TianshangPulse AF-detection model.

Approach: RR-interval feature extraction + lightweight classifier.

Rationale (measured): end-to-end deep models (LSTM/CNN) overfit the small
patient pool (train loss -> 0, val AUC ~0.74) because they memorize training
patients. RR-interval statistics extracted from PPG beat detection give
val AUC ~0.93 with only a handful of parameters, and map cleanly onto the
firmware (beat detector + small classifier).

Pipeline: beat detection -> RRI features -> classifier -> ONNX.

Usage:
    python scripts/train_af_model.py
    python scripts/train_af_model.py --model mlp --hidden 32 --epochs 200
"""

import argparse
import json
import os
import time

import numpy as np
from scipy.signal import find_peaks
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import (accuracy_score, balanced_accuracy_score,
                             confusion_matrix, f1_score, roc_auc_score)

import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

DATA_DIR = os.path.join("data", "processed")
OUT_DIR = os.path.join("model", "af")

FEATURES = ["rri_std", "rmssd", "pnn50", "cv", "hr_proxy", "diff_std"]


def extract_features(windows, fs=100.0, min_peaks=3):
    """Compute RRI statistics per window. Returns (N, n_features)."""
    N = windows.shape[0]
    F = np.zeros((N, len(FEATURES)))
    for i in range(N):
        s = windows[i, :, 0]
        peaks, _ = find_peaks(s, distance=int(fs * 0.4))
        if len(peaks) >= min_peaks:
            rri = np.diff(peaks) / fs * 1000.0
            F[i, 0] = np.std(rri)
            F[i, 1] = np.sqrt(np.mean(np.diff(rri) ** 2)) if len(rri) > 1 else 0.0
            F[i, 2] = np.mean(np.abs(np.diff(rri)) > 50.0) if len(rri) > 1 else 0.0
            F[i, 3] = np.std(rri) / np.mean(rri) if np.mean(rri) > 0 else 0.0
            F[i, 4] = len(peaks) / (windows.shape[1] / fs)
        F[i, 5] = np.std(np.diff(s))
    return F


def make_mlp(n_in, hidden=32):
    return nn.Sequential(
        nn.Linear(n_in, hidden), nn.ReLU(), nn.Dropout(0.2),
        nn.Linear(hidden, 1),
    )


def train_mlp(X, y, Xv, yv, hidden=32, epochs=300, lr=1e-3, seed=42, patience=30):
    torch.manual_seed(seed)
    np.random.seed(seed)
    model = make_mlp(X.shape[1], hidden)
    crit = nn.BCEWithLogitsLoss()
    opt = torch.optim.Adam(model.parameters(), lr=lr, weight_decay=1e-3)
    ds = TensorDataset(torch.tensor(X, dtype=torch.float32),
                       torch.tensor(y, dtype=torch.float32))
    dl = DataLoader(ds, batch_size=64, shuffle=True)
    Xv_t = torch.tensor(Xv, dtype=torch.float32)

    best_auc, best_state, best_ep, wait = 0.0, None, 0, 0
    for ep in range(1, epochs + 1):
        model.train()
        for xb, yb in dl:
            opt.zero_grad()
            loss = crit(model(xb).squeeze(-1), yb)
            loss.backward()
            opt.step()
        model.eval()
        with torch.no_grad():
            prob = torch.sigmoid(model(Xv_t)).squeeze(-1).numpy()
        auc = roc_auc_score(yv, prob)
        if auc > best_auc:
            best_auc, best_state, best_ep, wait = auc, \
                {k: v.clone() for k, v in model.state_dict().items()}, ep, 0
        else:
            wait += 1
            if wait >= patience:
                break
    model.load_state_dict(best_state)
    return model, best_auc, best_ep


def main():
    parser = argparse.ArgumentParser(description="Train AF-detection model (RRI features)")
    parser.add_argument("--model", choices=["lr", "mlp"], default="mlp")
    parser.add_argument("--hidden", type=int, default=32)
    parser.add_argument("--epochs", type=int, default=300)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--out-dir", default=OUT_DIR)
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    train = np.load(os.path.join(DATA_DIR, "train.npz"), allow_pickle=True)
    val = np.load(os.path.join(DATA_DIR, "val.npz"), allow_pickle=True)

    X = extract_features(train["windows"])
    y = train["labels"].astype(int)
    Xv = extract_features(val["windows"])
    yv = val["labels"].astype(int)

    assert not np.isnan(X).any() and not np.isnan(Xv).any()

    t0 = time.time()
    if args.model == "lr":
        clf = LogisticRegression(max_iter=5000, C=1.0)
        clf.fit(X, y)
        prob_tr = clf.predict_proba(X)[:, 1]
        prob_va = clf.predict_proba(Xv)[:, 1]
        model_type = "logistic_regression"
        n_params = clf.coef_.size + clf.intercept_.size
        pred_fn = clf.predict_proba
        best_epoch = "n/a"
    else:
        model, best_auc, best_ep = train_mlp(X, y, Xv, yv, hidden=args.hidden,
                                             seed=args.seed)
        model.eval()
        with torch.no_grad():
            prob_tr = torch.sigmoid(model(torch.tensor(X, dtype=torch.float32))).squeeze(-1).numpy()
            prob_va = torch.sigmoid(model(torch.tensor(Xv, dtype=torch.float32))).squeeze(-1).numpy()
        clf = model
        model_type = "mlp"
        n_params = sum(p.numel() for p in model.parameters())
        best_epoch = best_ep
        torch.save(model.state_dict(), os.path.join(args.out_dir, "af_model.pth"))
        dummy = torch.randn(1, len(FEATURES))
        torch.onnx.export(model, dummy, os.path.join(args.out_dir, "af_model.onnx"),
                          input_names=["features"], output_names=["logit"],
                          opset_version=13)
        best_auc = roc_auc_score(yv, prob_va)

    yp_tr = (prob_tr >= 0.5).astype(int)
    yp_va = (prob_va >= 0.5).astype(int)
    metrics = {
        "train": {
            "auc": roc_auc_score(y, prob_tr),
            "accuracy": accuracy_score(y, yp_tr),
            "balanced_accuracy": balanced_accuracy_score(y, yp_tr),
            "f1": f1_score(y, yp_tr),
        },
        "val": {
            "auc": roc_auc_score(yv, prob_va),
            "accuracy": accuracy_score(yv, yp_va),
            "balanced_accuracy": balanced_accuracy_score(yv, yp_va),
            "f1": f1_score(yv, yp_va),
            "confusion": confusion_matrix(yv, yp_va).tolist(),
        },
    }
    report = {
        "model": model_type,
        "params": n_params,
        "features": FEATURES,
        "best_epoch": best_epoch,
        "metrics": metrics,
        "data": {
            "train_windows": len(y),
            "val_windows": len(yv),
            "train_labels": {"af": int((y == 1).sum()), "non_af": int((y == 0).sum())},
            "val_labels": {"af": int((yv == 1).sum()), "non_af": int((yv == 0).sum())},
        },
        "seed": args.seed,
        "elapsed_sec": round(time.time() - t0, 1),
        "note": "RRI features from beat detection; 4s non-overlap windows @100Hz",
    }
    with open(os.path.join(args.out_dir, "af_model_report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print(f"\n=== {model_type.upper()} ({n_params:,} params) ===")
    print(f"train auc={metrics['train']['auc']:.4f}  acc={metrics['train']['accuracy']:.4f}")
    print(f"val   auc={metrics['val']['auc']:.4f}  acc={metrics['val']['accuracy']:.4f}")
    print(f"val f1={metrics['val']['f1']:.4f}  balanced_acc={metrics['val']['balanced_accuracy']:.4f}")
    print(f"confusion={metrics['val']['confusion']}")
    print(f"wrote report to {args.out_dir}")


if __name__ == "__main__":
    main()