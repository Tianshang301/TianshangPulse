# TianshangPulse 模型架构

## 1. 概述

- 端侧 AI：**1.5M 参数 INT8 LSTM**，实时推理延迟 **<10ms**
- 输入：PPG 信号（MAX30102） + 加速度（MPU6886）
- 输出：心率、血氧、异常事件 + 置信度
- 推理频率：25Hz（`KInferenceIntervalMs = 40ms`）

## 2. 量化流水线

```
PyTorch (FP32)
    ↓
ONNX 导出
    ↓
INT8 per-channel 量化（代表数据集 ≥1000 条样本 calibration）
    ↓
TFLite Micro flatbuffer（model_data.cc）
    ↓
firmware/main/tflite/inference_engine.cc 加载
```

## 3. 量化 Checklist

- [ ] 使用代表数据集（≥1000 条样本）进行 calibration
- [ ] 验证 INT8 输出与 FP32 输出的余弦相似度 > 0.95
- [ ] 确认 TFLite Micro 支持所有算子（LSTM/GRU 需检查 op resolver）
- [ ] 生成 `model_data.cc` 后验证数组大小与 `.tflite` 文件一致

## 4. 输入/输出张量

> 待填充：模型更新时在此同步张量维度（AGENTS.md §ML-Agent 约束）。

## 5. 模型版本

| 版本 | 结构 | 精度 | 延迟(PC) | 延迟(ESP32-P4) | 备注 |
|------|------|------|----------|----------------|------|
| v1.0.0 | 待定 | 待定 | 待定 | 待定 | 首个版本 |

每次模型更新必须更新 `benchmark.md`。
