"""TianshangPulse 外部验证数据集适配器。

每个适配器将原始数据解析为统一中间格式：
    - signals: list[dict{name, fs, ppg(np.ndarray), ref_beats(np.ndarray 采样点),
                         acc_x/y/z (可选), patient_id(str), label(str 原始)}]
    - 顶层常量: source, license, fs
统一由 scripts/prepare_dataset.py 消费（滤波->滑窗->100Hz 重采样->Z-score）。

现有训练流水线（scripts/prepare_af_dataset.py）的带通参数为 0.5-8Hz。
注意：带通滤波必须按各源原始采样率重新设计（nyquist 不同），
不能在 100Hz 重采样后才滤波，否则通带边界错位（隐形参数失配）。
"""
