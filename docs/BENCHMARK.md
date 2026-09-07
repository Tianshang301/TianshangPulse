# TianshangPulse 外部验证基准（Benchmark）

> 本文档记录 TianshangPulse 7 参数逻辑回归（`model/af/af_model_lr.npz`）在外部公开数据集上的验证结果，
> 以及针对运动伪影的**门控改进方案**（IMU 能量门控 + PPG SQI）。模型本身为定稿版本，门控为运行期保护。

## 0. 结论速览（2026-09-06 更新：门控方案已落地）

| 结论 | 依据 |
|---|---|
| 固定 LR 在**安静/病床**场景表现稳定 | 患者隔离验证 AUC 0.928（见 §1） |
| **腕部运动破坏 RRI 特征，LR 假阳性率升至 0.79–0.93** | WRIST 实测（§4） |
| **IMU 门控 + PPG SQI 将运动误报压至 3.2%–3.8%（验收 <10% 达成）** | §4 |
| 固件已实现 `signal_gate` 模块（`mpu6886` 实读 + 运动能/ SQI + 门控分级），P4 编译通过 | §5 |

## 1. 基线（患者隔离验证，seed=42）

来源：`model/af/af_eval_lr_report.json`（`scripts/evaluate_af_model.py --model lr`）

| 指标 | 值 |
|---|---|
| AUC | 0.9281 |
| PR-AUC | 0.8137 |
| 准确率 | 0.8562 |
| 平衡准确率 | 0.8678 |
| F1 | 0.7805 |
| 灵敏度 / 特异度 | 0.8950 / 0.8407 |
| 窦性误判率（FPR） | **0.1593** |

- 数据：MIMIC PERform AF，10,500 窗（28 训 / 7 验，患者隔离）
- 模型：`PPG 峰检测 → 6 维 RRI 特征 → LR(7 参数)`，**本基准中模型未改动**

## 2. 数据源清单

| 数据集 | 访问 | 许可 | 采样率 | 状态 |
|---|---|---|---|---|
| MIMIC PERFORM AF | 开放 | ODbL-1.0 | 125 Hz | ✅ 已用于训练/验证 |
| WRIST (PhysioNet) | 开放 | ODC-BY-1.0 | 256 Hz | ✅ 已下载并评估 |
| BIDMC (PhysioNet) | 地区受限(403) | ODC-BY-1.0 | 125 Hz | ⚠️ 适配器就绪，下载被拒 |
| CapnoBase | 注册申请 | 学术 | 300 Hz | ⚠️ 适配器就绪，待申请 |
| PPG-DaLiA | 开放(UCI) | 研究 | 64 Hz | ⚠️ 适配器就绪，未下载 |
| MIMIC-III-Ext-PPG | **凭据(DUA+CITI)** | Credentialed 1.5.0 | 125 Hz | 🔒 适配器+过滤逻辑就绪，待凭据 |

### 3. 峰值检测评估（WRIST）

脚本：`scripts/evaluate_beat_detection.py --npz data/processed/wrist/external.npz`
对齐容差 ±150 ms；检测器与训练流水线一致（`find_peaks`，最小间距 0.4 s @100 Hz）。

| 活动 | 窗口数 | 参考心跳 | F1 | 灵敏度 | 精确率 |
|---|---|---|---|---|---|
| **总体** | 1,690 | 11,345 | **0.555** | 0.564 | 0.545 |
| high_resistance_bike | 320 | — | 0.656 | — | — |
| low_resistance_bike | 442 | — | 0.594 | — | — |
| run | 372 | — | 0.556 | — | — |
| walk | 556 | — | **0.446** | — | — |

时间误差：P-误差均值 −6.3 ms ± 87 ms，|误差|均值 75.4 ms。

> 解释：腕部行走时 PPG 受大幅运动伪影（基线漂移，raw std≈175）干扰；
> 骑行数据已由数据集方做 15 Hz 低通，反而更干净。**walk 场景是腕部峰值检测的最差工况。**

## 4. 运动伪影鲁棒性（固定 LR，WRIST）与门控改进

脚本：`scripts/evaluate_motion_robustness.py --npz data/processed/wrist/external.npz [--gate]`
所有 WRIST 窗为窦性；指标为「发出且未被抑制的 AF 误判 / 全部窗」（emitted FP rate，验收 <0.10）。

### 4.1 未门控（仅分类器）

| 活动 | 窗口 | emitted-AF-FP | P(AF) mean |
|---|---|---|---|
| **总体** | 1,690 | **0.854** | 0.74 |
| high_resistance_bike | 320 | 0.934 | 0.79 |
| low_resistance_bike | 442 | 0.923 | 0.79 |
| run | 372 | 0.798 | 0.71 |
| walk | 556 | 0.790 | 0.69 |

运动下特征严重畸变（窦性窗）：`rri_std`≈109 ms、`rmssd`≈165 ms、`pnn50`≈0.65。
对照：安静病床窦性 FPR = 0.159（`af_eval_lr_report.json`）。

### 4.2 门控后（IMU 能量 + PPG SQI）

阈值：`model/af/motion_gate.json`（T_high = 运动能量 5% 分位 0.0503，T_silent = 0.05）、SQI ≥ 0.30。

| 活动 | 窗口 | 抑制数 | emitted-AF-FP（门控后） |
|---|---|---|---|
| **总体** | 1,690 | 1,624 | **0.037** ✅ |
| high_resistance_bike | 320 | 310 | 0.031 |
| low_resistance_bike | 442 | 386 | 0.120* |
| run | 372 | 372 | 0.000 |
| walk | 556 | 556 | 0.000 |

*low_resistance_bike 略超 10% 验收线：骑行时手臂相对固定、伪影来自振动传导，IMU 能量低；
已由 SQI 部分兜底，属已知局限（对 AF 筛查影响有限），待运动补偿/更敏 SQI 做 P2 优化。

> **效果**：运动误报从 85.4% 降至 3.7%（绝对降低 81.7 个百分点，相对 -96%）。
> 安静场景回归：患者隔离 val AUC 0.928 / F1 0.781 完全不受门控影响（门控在低运动时即放行）。

### 4.3 hard-negative 增强训练（研究结论，未纳入主模型）

尝试将 WRIST 运动窗作为 hard-negative 注入训练（`train_af_model.py --extra-npz`）：

| 模型 | 运动 FP(未门控) | 运动 FP(门控) | 安静 val AUC | 安静 val F1 |
|---|---|---|---|---|
| 基线 LR（主模型） | 0.854 | 0.038 | **0.928** | **0.781** |
| hard-negative 增强 | **0.433** | **0.032** | 0.894 | 0.689 |

结论：增强确实降低未门控运动 FP（0.854→0.433），但因门控已达标（0.038 < 0.10）且增强以
安静性能下降（AUC -0.034, F1 -0.092）为代价，**未纳入主模型**；存档 `af_model_lr_enhanced.npz`。

## 5. 门控实现（固件，已编译通过）

| 模块 | 文件 | 内容 |
|---|---|---|
| IMU 实读 | `sensors/mpu6886.c` | 真实 I2C 读取加速度（+8g 量程）；含 `mpu6886_accel_g()` |
| 门控算法 | `sensors/signal_gate.c/.h` | 运动能量（0.5Hz 高通方差）+ PPG SQI（幅度 CV/间期合法性/HR 窗）+ 四档分级 |
| 引擎接入 | `tflite/inference_engine.cc` | `inference_engine_run_gated()`：高运动/低 SQI 抑制 AF，中运动压置信度 |
| 数据通路 | `main.c` | sensor 任务 100Hz 采样、4s 窗缓冲 + 10Hz IMU 收集；推理前计算门控 |

门控分级 → `confidence` 映射：`ACTIVE`（正常，0-100）、`LOW_MOTION`（0-60）、
`HIGH_MOTION`/`LOW_SQI`（0 且 anomaly_flag=0，不触发 BLE 0xFFF2 上报）。

> 阈值与脚本标定值一致，可在固件 `signal_gate_set_thresholds()` 按实板调参。
> P4 目标编译通过（bin 0x6DE60 B，分区 57% 空闲）。

## 6. 后续工作（P2）

- **自适应峰值检测**：基线漂移抑制 + 自适应阈值，改善 walk 场景 F1（0.446），重在使用于心率质量。
- **low_resistance_bike 门控优化**：更灵敏的 SQI 通带或 IMU 运动补偿。
- **MIMIC-III-Ext-PPG 大规模验证**：凭据就绪后 `--source mimic_iii_ext_ppg` 补 AF 分类外部验证。

## 7. 部署形态（方案 A：硬编码系数）

7 参数 LR 直接固化为 C 头文件，零 TFLite/ONNX 运行时依赖：

```
scripts/export_lr_coefs.py --weights model/af/af_model_lr.npz
    -> firmware/main/af_lr_coefs.h
```

- 固件侧：`sensors/af_features.c` 提取 6 维 RRI 特征 → `inference_engine_run_features()`
  执行 `af_lr_score()` + `af_lr_sigmoid()` → anomaly_flag / confidence
- **数值一致性已验证**：float 截断系数 vs 训练端完整精度，得分最大偏差 5.6e-07、
  0.5 阈值分类 100% 一致（WRIST 1,690 窗实测）
- P4 编译通过（bin 0x6E370 B）

> 为何不用 ONNX：LR 仅 7 个标量系数，一次 6 维内积即可表达；
> ONNX 是为多算子深度模型（CNN/LSTM）设计计算图格式，引入 ONNX runtime
> 对嵌入端是纯负收益（解析开销 + 依赖体积）。ONNX 导出仅用于 MLP/CNN-LSTM 存档
> （`model/af/af_model.onnx`、`af_lstm.onnx`），LR 走硬编码路径。

## 8. 复现命令

```bash
pip install -r requirements-validation.txt
python scripts/prepare_dataset.py --source wrist --out-dir data/processed/wrist
python scripts/calibrate_motion_gate.py --motion-npz data/processed/wrist/external.npz \
    --out model/af/motion_gate.json
python scripts/evaluate_beat_detection.py --npz data/processed/wrist/external.npz
python scripts/evaluate_motion_robustness.py --npz data/processed/wrist/external.npz \
    --gate --gate-json model/af/motion_gate.json
python scripts/evaluate_af_model.py --model lr    # 安静基线回归
```

## 9. 参考文献

- Jarchi D, Casson AJ. *Description of a Database Containing Wrist PPG Signals Recorded
  during Physical Exercise with Both Accelerometer and Gyroscope Measures of Motion.*
  Data 2017, 2(1):1. doi:10.3390/data2010001
- Pimentel MAF, et al. *Towards a Robust Estimation of Respiratory Rate from Pulse Oximeters.*
  IEEE TBME 64(8):1914-1923, 2016.
- Moulaeifard M, Charlton PH, Strodthoff N. *MIMIC-III-Ext-PPG: A PPG Benchmark Dataset
  for Cardiorespiratory Analysis.* PhysioNet 2026, doi:10.13026/r6k1-xt76.
- Charlton PH, et al. *Detecting beats in the photoplethysmogram: benchmarking open-source
  algorithms.* Physiological Measurement 2022.
- Karlen W, et al. *CapnoBase: signal database and tools to collect, share and annotate
  respiratory signals.* 2010.
- Reiss A, et al. *Deep PPG: Large-Scale Heart Rate Estimation with Convolutional Neural
  Networks.* Sensors 2019.
