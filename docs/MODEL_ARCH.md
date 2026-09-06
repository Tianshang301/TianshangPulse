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
| 固件落地 | 峰值检测 → 特征 → 分类器（7 参数 LR 硬编码为 `firmware/main/af_lr_coefs.h`，零运行时依赖） |

> 训练脚本 `scripts/train_af_model.py`，数据准备 `scripts/prepare_af_dataset.py`（均固定 seed 可复现）。

### 4.1.1 7 参数语义解读（可解释性）

| 特征 | 系数 | 单位 | 解读 |
|---|---|---|---|
| `cv` | **+6.72** | 无 | **权重最大**：RRI 变异系数是房颤（RR 间期绝对不规则）的最强判别信号 |
| `pnn50` | **+4.28** | [0..1] | 次高：相邻 RRI 波动 >50ms 的占比，AF 常 >0.4 |
| `hr_proxy` | +1.26 | [1/s] | AF 常伴较快心室率，提供速率上下文 |
| `diff_std` | +0.18 | 无 | 波形形态微弱辅助 |
| `rri_std` | −0.0065 | ms | **负权重**：单看绝对波动会误伤"安静但心率高/间期长"者，模型倾向相对指标 |
| `rmssd` | −0.0025 | ms | 同 `rri_std`，负权重防绝对量主导 |
| `bias` | −3.73 | — | 全特征为 0（恒定 RRI）→ P(AF)≈0.023，基线判非 AF |

> **生理逻辑**：房颤 = RR 间期"绝对不规则"；模型通过 `cv`/`pnn50`（相对波动）捕获这一本质，
> 而给绝对量（`rri_std`/`rmssd`）负权重以避免把"窦性但高心率/大间期"误判为 AF。
> 7 个系数即模型全部决策逻辑，可直接在 `firmware/main/af_lr_coefs.h` 审阅；
> 附两例判读（窦性→P≈0.08 非 AF；房颤→P≈0.87 判 AF），固件可据此就地自测。

### 4.2 外部验证与运动门控（2026-09-06 更新）

| 场景 | 指标 | 结论 |
|---|---|---|
| 患者隔离验证（MIMIC PERFORM AF） | val AUC 0.928 / 窦性 FPR 0.159 | 病床/安静场景稳定（模型定稿） |
| **腕部运动（WRIST，8 人，窦性）** | 峰值检测 F1 0.45–0.66；LR AF 误报 0.79–0.93 | **运动伪影破坏 RRI 特征** |
| **门控改进后（IMU + PPG SQI）** | **运动误报降至 0.037（验收 <0.10）**；安静 AUC 0.928 不受影响 | **达成既定验收标准** |

> **门控方案**：MPU6886 运动能量（0.5Hz 高通方差）双阈值 + 轻量 PPG SQI（幅度 CV/间期合法性/HR 生理窗）。
> 固件实现：`sensors/signal_gate.c`（`mpu6886` 实读 + 门控分级）、`tflite/inference_engine.cc` 的
> `inference_engine_run_gated()`、`main.c` 数据通路。阈值标定 `scripts/calibrate_motion_gate.py`。
> 详见 `docs/BENCHMARK.md`。P2 待办：自适应峰值检测、bike 门控优化、
> MIMIC-III-Ext-PPG（凭据）大规模外部验证。

## 5. 模型版本

| 版本 | 结构 | 精度 | 延迟(PC) | 延迟(ESP32-P4) | 备注 |
|------|------|------|----------|----------------|------|
| v1.0.0 | LR(7) 特征分类 | 患者隔离 AUC 0.928 | 实测待补 | 待补 | 外部验证见 §4.2 |

> 每次模型更新必须更新 `benchmark.md`。
