# PLAN — P0 正确性修复 + 训练/推理对齐（阶段 1+2）

> 📌 **本文件记录的是"已完成"的计划**（P0 正确性修复 + 训练/推理对齐）。
> **当前进行中的计划见 [`PLAN_WATCH_INTEGRATION.md`](./PLAN_WATCH_INTEGRATION.md)**（与 TianshangHealth 的接线，Server 侧任务卡 P1–P9）。

> 状态：**已完成**（A1/A2/A3/A5/A6/B5 修复 + A4 预处理对齐 + 双端 parity 硬门全绿）

## 背景（2026-09-20 项目探索结论）

探索发现三类问题：
1. **P0 正确性**：max30102 空指针崩溃、sensor/inference 单缓冲数据竞争、
   vTaskDelay 采样漂移 + FIFO 静默丢样本、0xFFF4 批量同步丢失未读事件、
   offline_cache 跨任务无锁、任务栈未校验（AGENTS.md §4 红线）
2. **train/serve skew**：训练端逐窗 Z-score+clip±3 预处理在固件端缺失，
   峰值检测 `>0` 门限依赖 DC 偏置（真实 MAX30102 可能 DC<0）
3. **验证缺失**：固件纯函数（特征/门控/CRC）零测试

## 执行记录

### 阶段 1：P0 修复

| 项 | 文件 | 内容 | 验证 |
|---|---|---|---|
| A1 | `sensors/max30102.c` | `s_dev` NULL 防护（对齐 mpu6886 模式），设备缺失不崩溃 | host 编译 + P4/S3 编译 |
| A2 | `main.c` | 双缓冲 `s_ppg_win[2][400]`/IMU、发布槽临界区（`s_ready_idx`）、消费者忙丢窗保非重叠语义、40ms 轮询→`ulTaskNotifyTake` 事件驱动 | P4/S3 编译 |
| A3 | `max30102.c` | FIFO WR/RD 指针批量 drain（≤32 帧/事务）+ OVF_COUNTER 溢出检测（4-bit 环绕，30s 节流告警）；`vTaskDelayUntil` 定周期 100Hz | P4/S3 编译 |
| A5 | `offline_cache.{h,c}` + `gatt_server.c` | `peek_batch`（预览不删）/`pop(n)`（删最旧 n 条）；0xFFF4 Read→peek、ACK→精确 pop 交付条数（修复 >20 条时未读事件被误清） | host 单测 ALL PASS |
| A6 | `offline_cache.c` | `portMUX` 短临界区保护 push/peek/pop/count（inference vs NimBLE host task） | host 单测 |
| B5 | `main.c` | `xTaskCreate` 返回值检查、`esp_task_wdt` 订阅+喂狗（TWDT 未启用防御式忽略）、15s/1min 周期栈水位 HWM 日志 | P4/S3 编译 |

### 阶段 2：训练/推理对齐 + parity 测试

| 项 | 文件 | 内容 |
|---|---|---|
| A4 | `sensors/ppg_preprocess.{c,h}`（新） | 逐窗 Z-score+clip±3（与 `datasets/base.py::zscore_clip` 逐行一致：population std、下限 1e-8、clip ±3，C 侧 double 累加）；`main.c` 推理任务先归一化再 SQI/特征/HR |
| E1 | `tests/parity/parity_harness.c` + `scripts/parity_check.py`（新） | host 编译**真实固件源码**，numpy 参考实现（逐行同构）从同一 raw 窗对比；编译器发现 gcc/clang/cl/zig |

### 验证结果

- **双端 parity（train.npz，200 窗，硬门全绿 PASS）**：
  - n_peaks 100% 一致（0 mismatch）
  - 特征 max\|Δ\|：rri_std 1.8e-05 / rmssd 2.7e-05 / pnn50 3.3e-07 / cv 5.0e-07 / hr 0 / diff_std 5.1e-07
  - SQI max\|Δ\| 2.7e-06
  - 残余 skew（信息性）：scipy find_peaks 与 C 峰集合一致率 67.5%（plateau/首峰优先规则差异，待 P2 自适应峰检一并处理）
- **host 单测**：`tests/host/test_offline_cache.c`（+5 个桩头文件）——ring 边界/覆盖最旧/peek 不删/pop 精确清除/NULL 防护 ALL PASS
- **双目标编译**：见 git 提交记录（P4 + S3）
- **文档**：MODEL_ARCH.md §4.1、BENCHMARK.md §7 同步；编译器 ziglang 0.16.0（pip user，本机无 gcc/MSVC）

## 后续（未纳入本轮）

- scipy/C 峰值规则统一（plateau 取中点 + 保幅值优先），消除 67.5%→100% 的残余 skew
- 资源瘦身（阶段 3）：1MB PSRAM arena 按需分配、LVGL/TFLite Kconfig 化、IMU 10Hz 读取、MAX30102 LED 电流
- 补功能（阶段 4）：SpO2 通道（0x2A5F）、CCCD 订阅跟踪、BLE 配对加密、STANDBY 前缓存持久化
- QE（阶段 5）：GitHub Actions CI（ubuntu gcc 跑 parity + 双目标编译）、文档失同步项清理