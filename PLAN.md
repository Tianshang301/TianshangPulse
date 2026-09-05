# PLAN — 下载 MIMIC PERform AF 数据集 + 准备训练数据（审核修订版）

> 状态：**待执行**（参数已确认，采纳审核意见）

## 背景

本机网络对 `zenodo.org` 的 DNS 被污染（只返回 IPv6 且无路由），需强制 IPv4 直连下载。目标数据集为 MIMIC PERform AF Dataset，用于后续 LSTM AF 检测模型训练。

## 已确认参数

| 项 | 值 |
|---|---|
| 记录 | `15906524`（MIMIC PERform Datasets v2.0，ODbL-1.0 许可） |
| 格式 | CSV（`mimic_perform_af_csv.zip` 27MB + `mimic_perform_non_af_csv.zip` 23MB） |
| 范围 | 仅 AF 数据集（35 人：19 AF + 16 非 AF） |
| 信号 | PPG + ECG + 呼吸，125Hz，20 分钟/人 |
| 滤波域 | **125Hz 原始域**：0.5–8Hz 带通 + Z-score + 截断 ±3σ |
| 窗口长度 | **1 秒**（125 点）→ 降采样到 **100 点**（与固件 100Hz 对齐） |
| 滑窗步长 | **0.5 秒**（50% 重叠，~4.8 万窗；`--step-sec` 参数化，执行对比 0.2/0.5） |
| 模型输入 | **单通道 PPG `(100,1)`**（ECG 存 npz 备用，不参与输入） |
| 数据划分 | **按患者隔离** `GroupShuffleSplit`：训练 28 人 / 验证 7 人（8:2，seed 42） |

## 网络方案

- `curl.exe --resolve zenodo.org:443:<IPv4>` 强制 IPv4 直连（已实测 HTTP 206 可达）
- IPv4 候选：`137.138.153.219`、`137.138.52.235`、`188.184.98.114`
- 下载后校验字节数与预期一致（AF=27,206,049 / non-AF=23,217,818），不匹配则中止

## 执行步骤

1. **建目录**：`data/raw/mimic_perform_af/` + `data/processed/`
2. **下载**两个 CSV zip（`curl --resolve` 强制 IPv4），校验字节数
3. **解压**：确认 AF=19 / non-AF=16 受试者，核对 `_data.csv` 列（`Time,PPG,ECG`+可选`resp`）
4. **安装依赖**：`pip install scikit-learn`（`GroupShuffleSplit`）
5. **新增 `scripts/prepare_af_dataset.py`**：
   - 读取每受试者 `_data.csv` 的 PPG（及 ECG 备用）列
   - 预处理：0.5–8Hz bandpass → Z-score（按窗口）→ clip ±3σ
   - 滑窗：1 秒窗 / 0.5 秒步（参数化），125 点 → Resample 到 100 点
   - **GroupShuffleSplit 按 patient_id 划分**（28 训 / 7 验，seed 42）
   - 输出：
     - `data/processed/train.npz`：`windows (N,100,1)`、`labels (N,)`、`patient_id (N,)`
     - `data/processed/val.npz`：同格式
     - `data/processed/mimic_perform_af_meta.json`：受试者映射、窗数、划分明细
   - 内置断言：无 NaN、标签全 0/1、训练/验证患者无重叠
6. **运行脚本**，核验规模（预计 train ~3.8 万 / val ~1 万窗）
7. 数据文件由 `.gitignore` 排除（`data/*.csv`/`data/*.npz`），仅脚本入库

## 范围边界

- 本轮只下载 + 解析为训练用 npz，不训练模型、不烧录
- 数据集文件不进入 git（由 .gitignore 排除）

## 产出

- `data/raw/mimic_perform_af/`（原始 zip + 解压 CSV，不入库）
- `data/processed/train.npz` + `val.npz` + `meta.json`（不入库）
- `scripts/prepare_af_dataset.py`（入库）
- `docs/MODEL_ARCH.md` §4 张量维度同步（`(100,1)` 输入 + AF 输出扩展）
- 数据报告（受试者/窗数/时长分布）摘要

## 风险与备注

- DNS 污染若导致 `--resolve` 失效，切换其他 IPv4 候选地址重试
- CSV 解析需确认每受试者实际列数一致（部分含 `resp`）
- ECG 仅存备用，不参与模型输入（部署端无 ECG 传感器，避免上帝特征）
- 预计耗时：下载 ~50MB + 解析 4.8 万窗（数分钟）