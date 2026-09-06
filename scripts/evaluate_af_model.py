#!/usr/bin/env python3
"""Evaluate a trained TianshangPulse AF model on the patient-isolated val set.

Loads the trained model artifacts produced by `scripts/train_af_model.py` and
computes a complete set of validation metrics on data/processed/val.npz:

    - AUC (ROC), PR-AUC
    - accuracy, balanced accuracy, F1
    - sensitivity / specificity / precision
    - confusion matrix
    - accuracy by AF / non-AF and by patient group stats

Models:
    --model lr     loads model/af/af_model_lr.npz (7-param logistic regression)
    --model mlp    loads model/af/af_model.pth (MLP-32)

Usage:
    python scripts/evaluate_af_model.py --model lr
    python scripts/evaluate_af_model.py --model mlp
"""

import argparse
import json
import os

import numpy as np
import torch
from sklearn.metrics import (accuracy_score, average_precision_score,
                             balanced_accuracy_score, confusion_matrix,
                             f1_score, roc_auc_score, roc_curve)

from train_af_model import FEATURES, extract_features

DATA_DIR = os.path.join("data", "processed")
OUT_DIR = os.path.join("model", "af")


def sigmoid(z):
    return 1.0 / (1.0 + np.exp(-z))


def load_model(kind):
    """Return a function prob_af(X) -> (N,) probabilities on feature matrix."""
    if kind == "lr":
        p = os.path.join(OUT_DIR, "af_model_lr.npz")
        if not os.path.isfile(p):
            raise FileNotFoundError(f"{p} not found; run train_af_model.py --model lr first")
        w = np.load(p)
        coef, intercept = w["coef"], float(w["intercept"])

        def prob(X):
            return sigmoid(X @ coef + intercept)

        return prob, {"model": "logistic_regression", "params": int(coef.size + 1)}
    elif kind == "mlp":
        p = os.path.join(OUT_DIR, "af_model.pth")
        if not os.path.isfile(p):
            raise FileNotFoundError(f"{p} not found")
        net = torch.nn.Sequential(
            torch.nn.Linear(len(FEATURES), 32), torch.nn.ReLU(),
            torch.nn.Dropout(0.2), torch.nn.Linear(32, 1),
        )
        net.load_state_dict(torch.load(p, map_location="cpu", weights_only=True))
        net.eval()

        def prob(X):
            with torch.no_grad():
                return torch.sigmoid(
                    net(torch.tensor(X, dtype=torch.float32)).squeeze(-1)
                ).numpy()

        n_params = sum(t.numel() for t in net.parameters())
        return prob, {"model": "mlp", "params": n_params}
    else:
        raise ValueError(f"unknown model kind: {kind}")


def evaluate(X, y, prob, patient_id=None):
    pr = prob(X)
    yp = (pr >= 0.5).astype(int)

    tn, fp, fn, tp = confusion_matrix(y, yp).ravel()
    sens = tp / (tp + fn) if (tp + fn) else 0.0
    spec = tn / (tn + fp) if (tn + fp) else 0.0
    prec = tp / (tp + fp) if (tp + fp) else 0.0

    out = {
        "auc": float(roc_auc_score(y, pr)),
        "pr_auc": float(average_precision_score(y, pr)),
        "accuracy": float(accuracy_score(y, yp)),
        "balanced_accuracy": float(balanced_accuracy_score(y, yp)),
        "f1": float(f1_score(y, yp)),
        "sensitivity": float(sens),
        "specificity": float(spec),
        "precision": float(prec),
        "confusion": confusion_matrix(y, yp).tolist(),
        "n_af": int((y == 1).sum()),
        "n_non_af": int((y == 0).sum()),
    }

    if patient_id is not None:
        acc_by_p = {}
        for pid in np.unique(patient_id):
            m = patient_id == pid
            acc_by_p[str(pid)] = {
                "n": int(m.sum()),
                "accuracy": float(accuracy_score(y[m], yp[m])),
                "frac_af": float(y[m].mean()),
            }
        accs = [v["accuracy"] for v in acc_by_p.values()]
        out["patients"] = {
            "n": len(acc_by_p),
            "accuracy_mean": float(np.mean(accs)),
            "accuracy_std": float(np.std(accs)),
            "by_patient": acc_by_p,
        }

    for name in ("roc_tpr", "roc_fpr"):
        if name in locals():
            pass

    return out


def main():
    parser = argparse.ArgumentParser(description="Evaluate trained AF model on val set")
    parser.add_argument("--model", choices=["lr", "mlp"], required=True)
    parser.add_argument("--out-dir", default=OUT_DIR)
    args = parser.parse_args()

    prob, meta = load_model(args.model)
    val = np.load(os.path.join(DATA_DIR, "val.npz"), allow_pickle=True)
    Xv = extract_features(val["windows"])
    yv = val["labels"].astype(int)
    pid = val["patient_id"] if "patient_id" in val.files else None

    assert not np.isnan(Xv).any(), "NaN in val features"

    res = evaluate(Xv, yv, prob, patient_id=pid)
    report = {"model": meta, "split": "val", "metrics": res}
    out_path = os.path.join(args.out_dir, f"af_eval_{args.model}_report.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    m = res
    print(f"\n=== EVAL {meta['model']} ({meta['params']:,} params) on patient-isolated val set ===")
    print(f"n         = {m['n_af'] + m['n_non_af']}  (AF={m['n_af']}, non-AF={m['n_non_af']})")
    print(f"auc       = {m['auc']:.4f}")
    print(f"pr_auc    = {m['pr_auc']:.4f}")
    print(f"accuracy  = {m['accuracy']:.4f}")
    print(f"bal. acc  = {m['balanced_accuracy']:.4f}")
    print(f"f1        = {m['f1']:.4f}")
    print(f"sens/spec = {m['sensitivity']:.4f} / {m['specificity']:.4f}")
    print(f"precision = {m['precision']:.4f}")
    print(f"confusion = {m['confusion']}  (rows=truth AF/non-AF, cols=pred AF/non-AF)")
    if "patients" in m:
        print(f"per-patient acc mean={m['patients']['accuracy_mean']:.4f} "
              f"std={m['patients']['accuracy_std']:.4f} (n={m['patients']['n']})")
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()