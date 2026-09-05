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

> **⚠️ 实测修正（2026-09-05）**：端到端深度模型（LSTM/CNN）在小患者池上过拟合，val AUC ≤0.74。
> 端侧 AF 检测**改采 RR 间期特征 + 轻量分类器**路线（见 §4.1），原始波形 LSTM 方案已弃用。

### 4.1 AF 检测分类器（最终定档）

| 项 | 值 |
|---|---|
| 输入特征 | RR 间期统计：`rri_std` / `rmssd` / `pnn50` / `cv` / HR 代理 / 波形一阶差分 std（6 维） |
| 特征来源 | PPG 峰值检测（`scipy.signal.find_peaks`，min 间距 0.4s）→ 间期 → 统计量 |
| 分类器 | 逻辑回归（7 参数） |
| 输入窗口 | **4 秒非重叠窗口** @100Hz（400 点，含 ~3-4 心跳周期） |
| 验证结果 | 患者隔离 val AUC **0.928** / acc 0.856 / f1 0.781（seed 42） |
| 固件落地 | 峰值检测 → 特征 → 分类器（INT8 可部署，见 `docs/MEMORY_LAYOUT.md`） |

> 训练脚本 `scripts/train_af_model.py`，数据准备 `scripts/prepare_af_dataset.py`（均固定 seed 可复现）。

## 5. 模型版本

| 版本 | 结构 | 精度 | 延迟(PC) | 延迟(ESP32-P4) | 备注 |
|------|------|------|----------|----------------|------|
| v1.0.0 | 待定 | 待定 | 待定 | 待定 | 首个版本 |

每次模型更新必须更新 `benchmark.md`。
