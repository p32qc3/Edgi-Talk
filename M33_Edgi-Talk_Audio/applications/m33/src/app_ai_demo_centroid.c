/**

 * 演示四类：差分特征 + 余弦最近邻 + softmax 置信度（非 one-hot）。

 * 现场 demo_cal 用当前帧特征覆盖模板，与手机外放最匹配。

 */

#include "../include/app_ai.h"

#include "../include/edgi_demo_templates.h"

#include "../include/edgi_audio_capture.h"

#include "../include/edgi_spec_features.h"

#include "../include/protocol.h"

#include <finsh.h>

#include <math.h>

#include <stdlib.h>

#include <string.h>

#if EDGI_AI_USE_DEMO_CENTROID



typedef char edgi_demo_tpl_dim_ok[(EDGI_DEMO_TPL_DIM == 40u) ? 1 : -1];



static float s_live_tpl[4][40];

static float s_unit_tpl[4][40];

static volatile uint8_t s_cal_valid;

static volatile uint8_t s_tpl_unit_ready;

static float s_last_feat[40];



#ifndef EDGI_DEMO_COS_MIN

#define EDGI_DEMO_COS_MIN 0.52f

#endif

#ifndef EDGI_DEMO_ALARM_MIN_PROB

#define EDGI_DEMO_ALARM_MIN_PROB 0.38f

#endif

#ifndef EDGI_DEMO_SPEC_SUM_MIN

#define EDGI_DEMO_SPEC_SUM_MIN 22000u

#endif

#ifndef EDGI_DEMO_FEAT_ENERGY_MIN

#define EDGI_DEMO_FEAT_ENERGY_MIN 0.012f

#endif



static float edgi_vec_l2_sq(const float *v, unsigned n)

{

    float acc = 0.f;

    for (unsigned i = 0; i < n; i++)

        acc += v[i] * v[i];

    return acc;

}



static void edgi_vec_unit(const float *in, float *out, unsigned n)

{

    float n2 = edgi_vec_l2_sq(in, n);

    if (n2 < 1e-12f)

    {

        rt_memset(out, 0, n * sizeof(float));

        return;

    }

    float inv = 1.0f / sqrtf(n2);

    for (unsigned i = 0; i < n; i++)

        out[i] = in[i] * inv;

}



static const float *edgi_tpl_ptr_raw(unsigned idx)
{
    if (idx >= 4u)
        return edgi_demo_tpl[0];
    if (s_cal_valid & (1u << idx))
        return s_live_tpl[idx];
    return edgi_demo_tpl[idx];
}

static void edgi_rebuild_unit_tpl(void)
{
    for (unsigned i = 0; i < 4u; i++)
        edgi_vec_unit(edgi_tpl_ptr_raw(i), s_unit_tpl[i], EDGI_DEMO_TPL_DIM);
    s_tpl_unit_ready = 1u;
}



static const float *edgi_tpl_ptr(unsigned idx)

{

    if (!s_tpl_unit_ready)

        edgi_rebuild_unit_tpl();

    return s_unit_tpl[idx];

}



static float edgi_feat_energy(const float *x)

{

    return sqrtf(edgi_vec_l2_sq(x, EDGI_DEMO_TPL_DIM));

}



static void edgi_softmax_neg_dist(const float *neg_dist, int n, float *prob)

{

    float m = neg_dist[0];

    int i;

    float sum = 0.f;



    for (i = 1; i < n; i++)

        if (neg_dist[i] > m)

            m = neg_dist[i];



    for (i = 0; i < n; i++)

    {

        float t = expf(neg_dist[i] - m);

        prob[i] = t;

        sum += t;

    }

    if (sum > 0.f)

    {

        for (i = 0; i < n; i++)

            prob[i] /= sum;

    }

}



int app_ai_demo_centroid_init(void)

{

    rt_memset(s_live_tpl, 0, sizeof(s_live_tpl));

    s_cal_valid = 0u;

    s_tpl_unit_ready = 0u;

    edgi_rebuild_unit_tpl();

    return 0;

}



int app_ai_demo_centroid_infer(const uint8_t spec[40][40], float prob_out[ALARM_TYPE_MAX])

{

    float x[40];

    float xu[40];

    float cos_tpl[4];

    float alarm_prob[4];

    float spec_sum = 0;

    int i;

    int best_tpl = 0;

    float best_cos;

    float feat_e;



    for (i = 0; i < (int)ALARM_TYPE_MAX; i++)

        prob_out[i] = 0.f;



    for (int r = 0; r < 40; r++)

        for (int c = 0; c < 40; c++)

            spec_sum += spec[r][c];



    edgi_spec_row_features(spec, x);

    feat_e = edgi_feat_energy(x);



    if (spec_sum < EDGI_DEMO_SPEC_SUM_MIN || feat_e < EDGI_DEMO_FEAT_ENERGY_MIN)

    {

        prob_out[ALARM_TYPE_BACKGROUND] = 1.f;

        return 0;

    }



    rt_memcpy(s_last_feat, x, sizeof(s_last_feat));

    edgi_vec_unit(x, xu, EDGI_DEMO_TPL_DIM);

    if (edgi_vec_l2_sq(xu, EDGI_DEMO_TPL_DIM) < 1e-12f)

    {

        prob_out[ALARM_TYPE_BACKGROUND] = 1.f;

        return 0;

    }



    for (i = 0; i < 4; i++)

    {

        const float *t = edgi_tpl_ptr((unsigned)i);

        float dot = 0.f;

        for (unsigned k = 0; k < EDGI_DEMO_TPL_DIM; k++)

            dot += xu[k] * t[k];

        if (dot > 1.f)

            dot = 1.f;

        if (dot < -1.f)

            dot = -1.f;

        cos_tpl[i] = dot;

    }



    best_cos = cos_tpl[0];

    for (i = 1; i < 4; i++)

    {

        if (cos_tpl[i] > best_cos)

        {

            best_cos = cos_tpl[i];

            best_tpl = i;

        }

    }



    {

        float nd[4];

        for (i = 0; i < 4; i++)

            nd[i] = cos_tpl[i] * 8.0f;

        edgi_softmax_neg_dist(nd, 4, alarm_prob);

    }



    prob_out[ALARM_TYPE_BACKGROUND] = 1.f - alarm_prob[best_tpl];

    if (prob_out[ALARM_TYPE_BACKGROUND] < 0.f)

        prob_out[ALARM_TYPE_BACKGROUND] = 0.f;



    if (best_cos >= EDGI_DEMO_COS_MIN && alarm_prob[best_tpl] >= EDGI_DEMO_ALARM_MIN_PROB)

    {

        prob_out[ALARM_TYPE_BACKGROUND] = 1.f - alarm_prob[best_tpl];

        for (i = 0; i < 4; i++)

            prob_out[(int)ALARM_TYPE_FIRE + i] = alarm_prob[i];

        /* 归一化五类 */

        {

            float sum = 0.f;

            for (i = 0; i < (int)ALARM_TYPE_MAX; i++)

                sum += prob_out[i];

            if (sum > 0.f)

            {

                for (i = 0; i < (int)ALARM_TYPE_MAX; i++)

                    prob_out[i] /= sum;

            }

        }

    }

    else

    {

        for (i = 0; i < (int)ALARM_TYPE_MAX; i++)

            prob_out[i] = 0.f;

        prob_out[ALARM_TYPE_BACKGROUND] = 1.f;

    }



    return 0;

}



void edgi_demo_cal_store(uint8_t class_1_to_4)

{

    if (class_1_to_4 < 1u || class_1_to_4 > 4u)

        return;

    rt_memcpy(s_live_tpl[class_1_to_4 - 1u], s_last_feat, sizeof(s_last_feat));

    s_cal_valid |= (uint8_t)(1u << (class_1_to_4 - 1u));

    s_tpl_unit_ready = 0u;

    rt_kprintf("[CM33] demo_cal %u OK mask=0x%x\n",

               (unsigned)class_1_to_4, (unsigned)s_cal_valid);

}



void edgi_demo_cal_stat_print(void)

{

    rt_kprintf("[CM33] demo_cal mask=0x%x (bit0火 bit1敲 bit2婴 bit3水)\n",

               (unsigned)s_cal_valid);

    if (s_cal_valid != 0x0fu)

        rt_kprintf("[CM33] 未完成四类校准，播一类敲一次 demo_cal N\n");

}



static void demo_cal_print_help(void)

{

    rt_kprintf("demo_cal <1|2|3|4>  — 播对应声音时执行，写入现场模板\n");

    rt_kprintf("  1火警 2敲门 3婴儿 4水开；四路都 cal 后识别最稳\n");

    rt_kprintf("demo_cal stat       — 查看已校准位\n");

}



static void demo_cal_cmd(int argc, char **argv)

{

    int lab;



    if (argc < 2 || !rt_strcmp(argv[1], "stat"))

    {

        edgi_demo_cal_stat_print();

        return;

    }



    lab = atoi(argv[1]);

    if (lab < 1 || lab > 4)

    {

        demo_cal_print_help();

        return;

    }



    edgi_demo_cal_store((uint8_t)lab);

}



MSH_CMD_EXPORT_ALIAS(demo_cal_cmd, demo_cal, Calibrate demo sound template 1-4);

#else

int app_ai_demo_centroid_init(void)
{
    return 0;
}

int app_ai_demo_centroid_infer(const uint8_t spec[40][40], float prob_out[ALARM_TYPE_MAX])
{
    RT_UNUSED(spec);
    RT_UNUSED(prob_out);
    return -1;
}

void edgi_demo_cal_store(uint8_t class_1_to_4)
{
    RT_UNUSED(class_1_to_4);
}

void edgi_demo_cal_stat_print(void)
{
    rt_kprintf("[CM33] demo_cal disabled (EDGI_AI_USE_DEMO_CENTROID=0)\n");
}

#endif
