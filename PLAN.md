# PLAN — MIMIC PERform AF 数据集下载、解析与 AF 检测模型训练

> 状态：**已完成**（下载 → 解析 → 训练 → 提交推送）

## 已确认参数（最终定档）

| 项 | 值 |
|---|---|
| 数据源 | MIMIC PERform AF（zenodo 记录 `15906524`，ODbL-1.0，35 人：19 AF + 16 非 AF） |
| 信号 | PPG + ECG + 呼吸，125Hz，20 分钟/人 |
| 预处理 | 0.5–8Hz 带通 + 线性插值稀疏 NaN → **4 秒非重叠窗口** @100Hz（400 点）→ 逐窗 Z-score + clip ±3 |
| 数据划分 | **患者级隔离**（GroupShuffleSplit）：train 28 人 / val 7 人 |
| 训练数据 | 10,500 窗（AF 5,700 / non-AF 4,800） |
| 模型 | **RR 间期特征 + 逻辑回归**（7 参数，端侧可部署） |

## 执行记录

### 1. 下载（IPv4 workaround）
- `zenodo.org` DNS 被污染（仅返回 IPv6 无路由），用 `curl --resolve` 强制 IPv4 直连
- `mimic_perform_af_csv.zip`（27,206,049 B）+ `mimic_perform_non_af_csv.zip`（23,217,818 B），字节校验一致

### 2. 解析（`scripts/prepare_af_dataset.py`）
- 每受试者 CSV 含 `Time,PPG,ECG,resp`；稀疏 NaN 线性插值（避免丢受试者）
- 输出 `data/processed/train.npz` / `val.npz` / `meta.json`（`.gitignore` 排除，不入库）

### 3. 训练（`scripts/train_af_model.py`）
- **架构演进**：1s 窗 LSTM → 4s 窗 CNN-LSTM → **RR 间期特征 + LR/MLP**
- 深度模型（LSTM/CNN）在小患者池上过拟合（train loss→0 但 val AUC ≤0.74），放弃
- **最终方案**：峰值检测 → RR 间期统计特征（rri_std/rmssd/pnn50/cv/HR/波形 diff）→ 分类器

### 4. 训练结果（患者隔离，seed=42）

| 模型 | 参数 | val AUC | val acc | val f1 | 采用 |
|------|------|---------|---------|--------|------|
| 逻辑回归 | 7 | **0.928** | **0.856** | **0.781** | ✅ |
| MLP-32 | 257 | 0.916 | 0.791 | 0.720 | 备选 |

### 5. 提交
- 入库：`scripts/prepare_af_dataset.py`、`scripts/train_af_model.py`、`.gitignore` 更新
- 未入库：原始 CSV、npz、模型产物（pth/onnx/json）均在 `data/` 与 `model/af/`

## 结论

端侧 AF 检测采用 **特征工程 + 轻量分类器** 路线：固件端先做峰值检测提取 RR 间期，喂 7 参数分类器即可达 val AUC 0.93，远优于端到端深度模型在小样本上的表现。后续 INT8 量化与 TFLite Micro 部署按 AGENTS.md ML-Agent 流水线执行。

## 后续（未纳入本轮）

- INT8 量化（calibration 用本数据集，≥1000 样本）
- 固件 `inference_result_t` 增加 AF 输出字段（当前仅 HR/SpO2）
- `docs/MODEL_ARCH.md` §4 张量维度同步为特征输入
