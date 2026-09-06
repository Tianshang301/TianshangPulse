#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 门控分级（inference_result_t.confidence 的映射） */
typedef enum {
    SIGNAL_GATE_ACTIVE,      // 正常推理
    SIGNAL_GATE_LOW_MOTION,  // 中运动：保留 AF 输出但压低置信度(30-60)
    SIGNAL_GATE_HIGH_MOTION, // 高运动：抑制 AF 输出
    SIGNAL_GATE_LOW_SQI,     // 信号质量差：跳过该窗特征计算
} signal_gate_level_t;

typedef struct {
    float motion_energy;     // 本窗 IMU 运动能量（与 scripts/prepare_dataset.py 同算法规约）
    float sqi;               // 本窗 PPG 信号质量 0..1
    signal_gate_level_t level;
} signal_gate_result_t;

/* 初始化门控参数（阈值来自 model/af/motion_gate.json 标定与 ppg_sqi.py 约定）。
 * 阈值可通过 signal_gate_set_thresholds() 运行时覆盖。 */
void signal_gate_init(void);

/* 运动能量：三轴加速度矢量经 0.5Hz 高通后的方差。acc 每轴单位为 g。
 * n 为采样点数；acc 期望为 3 通道连续数组（x,y,z 交错）或三独立指针。
 * 简化接口：单轴版本按注释约定——固件侧以三轴分开传入。 */
float signal_gate_motion_energy(const int16_t *ax, const int16_t *ay,
                                const int16_t *az, size_t n, uint32_t sample_rate_hz);

/* PPG 信号质量：与 scripts/ppg_sqi.py 同算法。win 为 4s@100Hz 归一化窗口。 */
float signal_gate_ppg_sqi(const float *win, size_t len, float fs, int *n_peaks);

/* 综合门控：给定运动能量与 SQI，返回分级。 */
signal_gate_result_t signal_gate_evaluate(float motion_energy, float sqi);

void signal_gate_set_thresholds(float t_silent, float t_high, float sqi_min);

#ifdef __cplusplus
}
#endif