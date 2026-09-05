# TianshangPulse 模型架构

## 1. 概述

- 端侧 AI：**750K 参数 INT8 LSTM**，实时推理延迟 **<10ms**
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

| 张量 | 维度 | 类型 | 说明 |
|------|------|------|------|
| 输入 | `(1, 100, 1)` | float32 → INT8 | 单通道 PPG，1 秒窗口 @100Hz（`KSensorSampleRateHz=100`） |
| 输出 | `(1, 3)` | INT8 | [HR, SpO2, AF 概率]，AF∈[0,1] |

- 训练数据来源：MIMIC PERform AF（记录 `15906524`，`data/processed/train.npz`），125Hz 原始 → 0.5–8Hz 带通 → 1 秒窗 → 重采样 100 点
- 患者级隔离划分（GroupShuffleSplit，28 训 / 7 验）
- 每输入一次推理需最近 1 秒 PPG 历史（固件维护滑动缓冲，见 `KSensorBufferSize`）

## 5. 模型版本

| 版本 | 结构 | 精度 | 延迟(PC) | 延迟(ESP32-P4) | 备注 |
|------|------|------|----------|----------------|------|
| v1.0.0 | 待定 | 待定 | 待定 | 待定 | 首个版本 |

每次模型更新必须更新 `benchmark.md`。
