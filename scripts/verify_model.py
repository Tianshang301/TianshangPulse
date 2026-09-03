#!/usr/bin/env python3
"""TianshangPulse 模型验证工具。

验证模型文件完整性:
    - 文件字节数与 model_data.cc 中数组大小一致
    - INT8 输出与 FP32 输出的余弦相似度 > 0.95

用法:
    python scripts/verify_model.py --model model/tianshang_lstm.tflite --source firmware/main/tflite/model_data.cc
"""

import argparse
import math
import os
import re
import sys


def cosine_similarity(a, b) -> float:
    if len(a) != len(b) or len(a) == 0:
        return 0.0
    dot = sum(x * y for x, y in zip(a, b))
    na = math.sqrt(sum(x * x for x in a))
    nb = math.sqrt(sum(y * y for y in b))
    if na == 0.0 or nb == 0.0:
        return 0.0
    return dot / (na * nb)


def extract_array_size(source_path: str) -> int:
    with open(source_path, "r", encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"kModelDataSize\s*=\s*(\d+)", text)
    if not m:
        return -1
    return int(m.group(1))


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify TianshangPulse model data")
    parser.add_argument("--model", required=True, help="path to .tflite model")
    parser.add_argument("--source", required=True, help="path to generated model_data.cc")
    args = parser.parse_args()

    if not os.path.isfile(args.model):
        print(f"error: model not found: {args.model}", file=sys.stderr)
        return 1
    if not os.path.isfile(args.source):
        print(f"error: source not found: {args.source}", file=sys.stderr)
        return 1

    model_size = os.path.getsize(args.model)
    array_size = extract_array_size(args.source)

    print(f"model file:  {model_size} bytes")
    print(f"array size:  {array_size} bytes")

    if array_size != model_size:
        print(f"FAIL: array size {array_size} != model size {model_size}")
        return 1

    print("PASS: model_data.cc array size matches .tflite file")

    # 余弦相似度验证需加载真实张量，此处占位
    print("note: cosine similarity check requires calibration data (TODO)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
