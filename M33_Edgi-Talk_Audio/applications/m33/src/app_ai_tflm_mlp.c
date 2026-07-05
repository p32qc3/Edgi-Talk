/**
 * 嵌入式真推理（轻量级）：40 维「行均值」特征 + 2 层 ReLU MLP + Softmax。
 * 权重由 tools/generate_edgi_mlp_weights.py 从 sklearn 导出到 edgi_mlp_weights.h。
 * 若需 TensorFlow Lite Micro + .tflite，请替换本文件并在工程中链接 TFLM。
 */
#include "../include/protocol.h"
#include "../include/edgi_mlp_weights.h"
#include "../include/edgi_spec_features.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* 勿用 #if 与 ALARM_TYPE_MAX 比较：枚举对预处理器不可见，会误报。 */
typedef char edgi_mlp_out_dim_matches_protocol[(EDGI_MLP_OUT_DIM == (unsigned)ALARM_TYPE_MAX) ? 1 : -1];

static void edgi_softmax_alarm(const float *logits, float *prob_out)
{
    float m = logits[0];
    for (int i = 1; i < (int)ALARM_TYPE_MAX; i++)
        if (logits[i] > m)
            m = logits[i];
    float sum = 0.f;
    for (int i = 0; i < (int)ALARM_TYPE_MAX; i++)
    {
        float t = expf(logits[i] - m);
        prob_out[i] = t;
        sum += t;
    }
    if (sum > 0.f)
    {
        for (int i = 0; i < (int)ALARM_TYPE_MAX; i++)
            prob_out[i] /= sum;
    }
}

int app_ai_tflm_init(void)
{
    return 0;
}

void app_ai_tflm_deinit(void)
{
}

int app_ai_tflm_infer(const uint8_t spec[40][40], float prob_out[ALARM_TYPE_MAX])
{
    float x[40];
    edgi_spec_row_features(spec, x);

    float h[EDGI_MLP_HIDDEN];
    for (unsigned j = 0; j < EDGI_MLP_HIDDEN; j++)
    {
        float acc = edgi_mlp_b1[j];
        for (unsigned i = 0; i < EDGI_MLP_IN_DIM; i++)
            acc += x[i] * edgi_mlp_w1[i * EDGI_MLP_HIDDEN + j];
        h[j] = acc > 0.f ? acc : 0.f;
    }

    float logits[ALARM_TYPE_MAX];
    for (unsigned k = 0; k < EDGI_MLP_OUT_DIM; k++)
    {
        float acc = edgi_mlp_b2[k];
        for (unsigned j = 0; j < EDGI_MLP_HIDDEN; j++)
            acc += h[j] * edgi_mlp_w2[j * EDGI_MLP_OUT_DIM + k];
        logits[k] = acc;
    }

    edgi_softmax_alarm(logits, prob_out);
    return 0;
}
