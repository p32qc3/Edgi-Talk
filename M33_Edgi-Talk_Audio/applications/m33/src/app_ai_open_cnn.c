/**
 * TensorFlow Micro Speech 风格轻量 CNN（Apache-2.0 架构参考），纯 C 推理。
 * 输入：49×40 log-mel；输出：5 类 softmax 概率。
 */
#include "../include/app_ai.h"
#include "../include/edgi_mel_frontend.h"
#include "../include/edgi_open_cnn_weights.h"
#include "../include/protocol.h"
#include <math.h>
#include <rtthread.h>
#include <string.h>

typedef char edgi_open_cnn_classes_ok[(EDGI_OPEN_CNN_CLASSES == (unsigned)ALARM_TYPE_MAX) ? 1 : -1];

#define EDGI_CNN_POOL4 4u

/* 49/4=12, 40/4=10 */
#define EDGI_CNN_H1 12u
#define EDGI_CNN_W1 10u

static float s_c0[EDGI_MEL_N_FRAMES * EDGI_MEL_N_BANDS * EDGI_OPEN_CNN_CONV0_FILTERS];
static float s_p1[EDGI_CNN_H1 * EDGI_CNN_W1 * EDGI_OPEN_CNN_CONV0_FILTERS];
static float s_c1[EDGI_CNN_H1 * EDGI_CNN_W1 * EDGI_OPEN_CNN_CONV1_FILTERS];
/* Single AI thread: keep CNN scratch static to avoid overflowing the RT-Thread stack. */
static float s_mel[EDGI_MEL_N_FRAMES][EDGI_MEL_N_BANDS];
static float s_gap[EDGI_OPEN_CNN_CONV1_FILTERS];
static float s_d0[EDGI_OPEN_CNN_DENSE0];
static float s_logits[EDGI_OPEN_CNN_CLASSES];

static float edgi_relu(float x)
{
    return x > 0.f ? x : 0.f;
}

static void edgi_softmax5(const float *logits, float *prob)
{
    float m = logits[0];
    float sum = 0.f;
    unsigned i;

    for (i = 1; i < EDGI_OPEN_CNN_CLASSES; i++)
        if (logits[i] > m)
            m = logits[i];

    for (i = 0; i < EDGI_OPEN_CNN_CLASSES; i++)
    {
        float t = expf(logits[i] - m);
        prob[i] = t;
        sum += t;
    }
    if (sum > 0.f)
    {
        for (i = 0; i < EDGI_OPEN_CNN_CLASSES; i++)
            prob[i] /= sum;
    }
}

static void edgi_conv2d_same(const float *in, unsigned ih, unsigned iw, unsigned ic,
                             const float *w, const float *b, unsigned kh, unsigned kw,
                             unsigned oc, float *out)
{
    unsigned oh = ih;
    unsigned ow = iw;
    unsigned y, x, o, ky, kx, c;

    for (o = 0; o < oc; o++)
    {
        for (y = 0; y < oh; y++)
        {
            for (x = 0; x < ow; x++)
            {
                float acc = b[o];
                for (c = 0; c < ic; c++)
                {
                    for (ky = 0; ky < kh; ky++)
                    {
                        int sy = (int)y + (int)ky - (int)(kh / 2u);
                        if (sy < 0 || sy >= (int)ih)
                            continue;
                        for (kx = 0; kx < kw; kx++)
                        {
                            int sx = (int)x + (int)kx - (int)(kw / 2u);
                            if (sx < 0 || sx >= (int)iw)
                                continue;
                            unsigned wi = ((ky * kw + kx) * ic + c) * oc + o;
                            acc += in[(sy * iw + sx) * ic + c] * w[wi];
                        }
                    }
                }
                out[(y * ow + x) * oc + o] = edgi_relu(acc);
            }
        }
    }
}

static void edgi_maxpool4(const float *in, unsigned ih, unsigned iw, unsigned ic, float *out)
{
    unsigned oh = ih / EDGI_CNN_POOL4;
    unsigned ow = iw / EDGI_CNN_POOL4;
    unsigned y, x, c, dy, dx;

    for (y = 0; y < oh; y++)
    {
        for (x = 0; x < ow; x++)
        {
            for (c = 0; c < ic; c++)
            {
                float m = -1e30f;
                for (dy = 0; dy < EDGI_CNN_POOL4; dy++)
                {
                    for (dx = 0; dx < EDGI_CNN_POOL4; dx++)
                    {
                        float v = in[((y * EDGI_CNN_POOL4 + dy) * iw + (x * EDGI_CNN_POOL4 + dx)) * ic + c];
                        if (v > m)
                            m = v;
                    }
                }
                out[(y * ow + x) * ic + c] = m;
            }
        }
    }
}

static void edgi_gap(const float *in, unsigned n, unsigned ic, float *out)
{
    unsigned c, i;
    for (c = 0; c < ic; c++)
    {
        float acc = 0.f;
        for (i = 0; i < n; i++)
            acc += in[i * ic + c];
        out[c] = acc / (float)n;
    }
}

static void edgi_dense(const float *in, unsigned in_dim, const float *w, const float *b,
                       unsigned out_dim, float *out, rt_bool_t relu_on)
{
    unsigned o, i;
    for (o = 0; o < out_dim; o++)
    {
        float acc = b[o];
        for (i = 0; i < in_dim; i++)
            acc += in[i] * w[i * out_dim + o];
        out[o] = relu_on ? edgi_relu(acc) : acc;
    }
}

int app_ai_open_cnn_init(void)
{
    edgi_mel_ring_reset();
    return 0;
}

int app_ai_open_cnn_push_pcm(const int16_t *pcm, unsigned nsamples)
{
    edgi_mel_ring_push(pcm, nsamples);
    return 0;
}

int app_ai_open_cnn_infer(float prob_out[ALARM_TYPE_MAX])
{
    unsigned i;

    for (i = 0; i < (unsigned)ALARM_TYPE_MAX; i++)
        prob_out[i] = 0.f;

    if (edgi_mel_ring_fill() < EDGI_MEL_CLIP_SAMPLES)
    {
        prob_out[ALARM_TYPE_BACKGROUND] = 1.f;
        return -1;
    }
    if (edgi_mel_compute(s_mel) != 0)
    {
        prob_out[ALARM_TYPE_BACKGROUND] = 1.f;
        return -1;
    }

    edgi_conv2d_same(&s_mel[0][0], EDGI_MEL_N_FRAMES, EDGI_MEL_N_BANDS, 1u,
                     edgi_open_cnn_conv0_w, edgi_open_cnn_conv0_b,
                     EDGI_OPEN_CNN_CONV0_KH, EDGI_OPEN_CNN_CONV0_KW,
                     EDGI_OPEN_CNN_CONV0_FILTERS, s_c0);
    edgi_maxpool4(s_c0, EDGI_MEL_N_FRAMES, EDGI_MEL_N_BANDS, EDGI_OPEN_CNN_CONV0_FILTERS, s_p1);
    edgi_conv2d_same(s_p1, EDGI_CNN_H1, EDGI_CNN_W1, EDGI_OPEN_CNN_CONV0_FILTERS,
                     edgi_open_cnn_conv1_w, edgi_open_cnn_conv1_b,
                     EDGI_OPEN_CNN_CONV1_KH, EDGI_OPEN_CNN_CONV1_KW,
                     EDGI_OPEN_CNN_CONV1_FILTERS, s_c1);
    edgi_gap(s_c1, EDGI_CNN_H1 * EDGI_CNN_W1, EDGI_OPEN_CNN_CONV1_FILTERS, s_gap);
    edgi_dense(s_gap, EDGI_OPEN_CNN_CONV1_FILTERS, edgi_open_cnn_dense0_w, edgi_open_cnn_dense0_b,
               EDGI_OPEN_CNN_DENSE0, s_d0, RT_TRUE);
    edgi_dense(s_d0, EDGI_OPEN_CNN_DENSE0, edgi_open_cnn_dense1_w, edgi_open_cnn_dense1_b,
               EDGI_OPEN_CNN_CLASSES, s_logits, RT_FALSE);
    edgi_softmax5(s_logits, prob_out);
    return 0;
}
