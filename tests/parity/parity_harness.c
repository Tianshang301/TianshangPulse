/* TianshangPulse 双端对齐测试 harness（host 编译运行，非固件目标）。
 *
 * 编译并运行与固件完全一致的处理链：
 *   ppg_preprocess_zscore_clip -> signal_gate_ppg_sqi -> af_features_extract
 *
 * 输入（little-endian 二进制，由 scripts/parity_check.py 生成）：
 *   [int32 n_windows] 之后每窗 [int32 len][len x float32（raw，未归一化）]
 * 输出（文本，每窗一行）：
 *   n_peaks sqi f0(rri_std) f1(rmssd) f2(pnn50) f3(cv) f4(hr) f5(diff_std)
 *
 * 依赖的三个固件源文件均不包含 ESP-IDF 头，可在任意主机编译器下编译：
 *   firmware/main/sensors/{ppg_preprocess,signal_gate,af_features}.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "sensors/ppg_preprocess.h"
#include "sensors/signal_gate.h"
#include "sensors/af_features.h"

#define MAX_WINDOW_BYTES (1 << 20)

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <input.bin> <output.txt>\n", argv[0]);
        return 2;
    }

    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        perror(argv[1]);
        return 2;
    }
    FILE *out = fopen(argv[2], "w");
    if (!out) {
        perror(argv[2]);
        fclose(in);
        return 2;
    }

    int32_t n_windows = 0;
    if (fread(&n_windows, sizeof(int32_t), 1, in) != 1 || n_windows <= 0) {
        fprintf(stderr, "bad header\n");
        fclose(in);
        fclose(out);
        return 2;
    }

    const float fs = 100.0f;   /* 与固件 KSensorSampleRateHz 一致 */

    for (int32_t w = 0; w < n_windows; w++) {
        int32_t len = 0;
        if (fread(&len, sizeof(int32_t), 1, in) != 1 || len <= 0 ||
            len > MAX_WINDOW_BYTES) {
            fprintf(stderr, "bad window %d header\n", w);
            break;
        }

        float *raw = (float *)malloc((size_t)len * sizeof(float));
        float *norm = (float *)malloc((size_t)len * sizeof(float));
        if (!raw || !norm || fread(raw, sizeof(float), (size_t)len, in) != (size_t)len) {
            fprintf(stderr, "truncated window %d\n", w);
            free(raw);
            free(norm);
            break;
        }

        ppg_preprocess_zscore_clip(raw, norm, (size_t)len);

        int n_peaks = 0;
        float sqi = signal_gate_ppg_sqi(norm, (size_t)len, fs, &n_peaks);
        float feat[6] = {0};
        int np_feat = af_features_extract(norm, (size_t)len, fs, feat);

        fprintf(out, "%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
                np_feat, (double)sqi,
                (double)feat[0], (double)feat[1], (double)feat[2],
                (double)feat[3], (double)feat[4], (double)feat[5]);

        free(raw);
        free(norm);
    }

    fclose(in);
    fclose(out);
    return 0;
}