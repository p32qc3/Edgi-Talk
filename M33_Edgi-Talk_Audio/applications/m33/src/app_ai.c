#include "../include/app_ai.h"
#include "../include/edgi_audio_capture.h"
#include "../include/edgi_voice_m33.h"
#include "../include/edgi_m33_m55_shm.h"
#include <rtthread.h>
#include <string.h>
#include <stdlib.h>

/* 1=能量分档占位；0=MLP 真推理（app_ai_tflm_mlp.c + edgi_mlp_weights.h） */
#ifndef EDGI_USE_MODEL_STUB
#define EDGI_USE_MODEL_STUB 0
#endif

/* 同类结果重复上报间隔（毫秒），减轻 UI/队列压力 */
#ifndef EDGI_AI_MQ_INTERVAL_MS
#define EDGI_AI_MQ_INTERVAL_MS 1000u
#endif

/*
 * whd_res_download 等 YMODEM 时置 0，关闭 [AI] 周期性打印，避免与 M55 共串口冲包。
 * 传完 WiFi 资源后改回 1。
 */
#ifndef EDGI_AI_SERIAL_LOG
#define EDGI_AI_SERIAL_LOG 1
#endif

/* 1=串口周期性打印 [AI] score；0=关闭（不影响推理/共享内存/M55 上云） */
#ifndef EDGI_AI_SCORE_LOG
#define EDGI_AI_SCORE_LOG 0
#endif
#ifndef EDGI_AI_SCORE_LOG_INTERVAL_MS
#define EDGI_AI_SCORE_LOG_INTERVAL_MS 1000u
#endif

/*
 * 量产推理：保持 EDGI_USE_MODEL_STUB 为 0（见文件顶部），否则走能量分档占位而非真实模型。
 *
 * 推理侧稳定化（默认开启）：EMA 平滑 softmax、非环境最低置信度、进出环境滞回。
 * 调参指引（嘈杂/漏报时逐项微调）：
 *   EDGI_AI_STAB_EMA_ALPHA     越大曲线越钝、越不爱切换类别。
 *   EDGI_AI_STAB_MIN_NON_BG_PROB 提高→更不易判非环境（减误报，可能增漏报）。
 *   EDGI_AI_STAB_ENTER_NON_BG_FRAMES 提高→进入告警更慢（更稳，延迟更大）。
 *   EDGI_AI_STAB_RELEASE_BG_FRAMES   提高→退出告警更慢。
 *   EDGI_AI_STAB_SWITCH_ALARM_FRAMES 提高→同类间切换更慢。
 * 混淆矩阵与误报率需在 PC 上用离线 wav + 固定测试集统计；板侧可配合 EDGI_AI_SERIAL_LOG 抽样打印。
 * 设为 0 可恢复为每帧直接 argmax。
 */
#ifndef EDGI_AI_STABILIZE
#define EDGI_AI_STABILIZE 1
#endif
#ifndef EDGI_AI_STAB_EMA_ALPHA
#if EDGI_AI_FIELD_TUNING
#define EDGI_AI_STAB_EMA_ALPHA 0.70f
#else
#define EDGI_AI_STAB_EMA_ALPHA 0.82f
#endif
#endif
#ifndef EDGI_AI_STAB_MIN_NON_BG_PROB
#if EDGI_AI_FIELD_TUNING
#define EDGI_AI_STAB_MIN_NON_BG_PROB 0.12f
#else
#define EDGI_AI_STAB_MIN_NON_BG_PROB 0.40f
#endif
#endif
#ifndef EDGI_AI_ALARM_MIN_PROB
#define EDGI_AI_ALARM_MIN_PROB 0.70f
#endif
#ifndef EDGI_AI_FIRE_MIN_PROB
#define EDGI_AI_FIRE_MIN_PROB 0.95f
#endif
#ifndef EDGI_AI_KNOCK_MIN_PROB
#define EDGI_AI_KNOCK_MIN_PROB 0.90f
#endif
#ifndef EDGI_AI_BABY_MIN_PROB
#define EDGI_AI_BABY_MIN_PROB 0.82f
#endif
#ifndef EDGI_AI_BOILING_MIN_PROB
#define EDGI_AI_BOILING_MIN_PROB 0.80f
#endif
#ifndef EDGI_AI_ALARM_MIN_MARGIN_BG
#define EDGI_AI_ALARM_MIN_MARGIN_BG 0.20f
#endif
#ifndef EDGI_AI_ALARM_MIN_MARGIN_NEXT
#define EDGI_AI_ALARM_MIN_MARGIN_NEXT 0.18f
#endif
#ifndef EDGI_AI_STAB_BG_MARGIN
#if EDGI_AI_FIELD_TUNING
#define EDGI_AI_STAB_BG_MARGIN 0.28f
#else
#define EDGI_AI_STAB_BG_MARGIN 0.18f
#endif
#endif
#ifndef EDGI_AI_STAB_ENTER_NON_BG_FRAMES
#if EDGI_AI_FIELD_TUNING
#define EDGI_AI_STAB_ENTER_NON_BG_FRAMES 1u
#else
#define EDGI_AI_STAB_ENTER_NON_BG_FRAMES 4u
#endif
#endif
#if EDGI_AI_USE_DEMO_CENTROID
#undef EDGI_AI_STAB_ENTER_NON_BG_FRAMES
#define EDGI_AI_STAB_ENTER_NON_BG_FRAMES 4u
#endif
#ifndef EDGI_AI_STAB_RELEASE_BG_FRAMES
/* 连续多少帧门控为环境才从告警退回环境 */
#define EDGI_AI_STAB_RELEASE_BG_FRAMES 10u
#endif
#ifndef EDGI_AI_STAB_SWITCH_ALARM_FRAMES
/* 已在某类告警时，切到另一类非环境需连续帧数 */
#define EDGI_AI_STAB_SWITCH_ALARM_FRAMES 4u
#endif

/*
 * 省电：静音阶段跳过频谱 + 推理（CPU/NPU 不算指令≈文档「空闲不高推理」）。
 * 与「整颗 MCU DeepSleep + 有声中断唤醒」不同：双核+PDM DMA+WiFi 需单独评估，见文档说明。
 * 启用：改为 1，并在安静环境下标定 EDGI_AI_GATE_MEAN_SQ_MAX（可暂时打印 msq）。
 */
#ifndef EDGI_AI_ENERGY_GATE
#define EDGI_AI_ENERGY_GATE 0
#endif
#ifndef EDGI_AI_GATE_MEAN_SQ_MAX
/* int16 PCM 单帧均方能量；低于则视为静音。麦克风增益不同需重标定 */
#define EDGI_AI_GATE_MEAN_SQ_MAX (500000u)
#endif
#ifndef EDGI_AI_GATE_ENTER_FRAMES
#define EDGI_AI_GATE_ENTER_FRAMES 8u
#endif
#ifndef EDGI_AI_SILENCE_YIELD_MS
#define EDGI_AI_SILENCE_YIELD_MS 30u
#endif

#ifndef EDGI_AI_PAUSE_DURING_WIFI_JOIN
#define EDGI_AI_PAUSE_DURING_WIFI_JOIN 1
#endif
#ifndef EDGI_AI_STARTUP_GUARD_MS
#define EDGI_AI_STARTUP_GUARD_MS 20000u
#endif

#if EDGI_AI_FIELD_TUNING
/* 演示：在 1~4 类里取最高；任一 >=12% 即输出该类（不再被 bg~60% 压成 out=0） */
#ifndef EDGI_AI_FIELD_MIN_ALARM_PROB
#define EDGI_AI_FIELD_MIN_ALARM_PROB 0.12f
#endif
#endif

#define AI_THREAD_STACK  8192
#define AI_THREAD_PRIO   21

static uint8_t prob_to_pct_u8(float v)
{
    float pct = v * 100.0f;
    if (pct > 100.f)
        pct = 100.f;
    if (pct < 0.f)
        pct = 0.f;
    return (uint8_t)(pct + 0.5f);
}

#if EDGI_AI_SCORE_LOG
static void ai_score_log(const float *p, int stable_class, float stable_conf)
{
    static rt_tick_t s_last_score_log;
    rt_tick_t now = rt_tick_get();
    rt_tick_t gap = rt_tick_from_millisecond(EDGI_AI_SCORE_LOG_INTERVAL_MS);

    if ((now - s_last_score_log) < gap)
        return;
    s_last_score_log = now;

    rt_kprintf("[AI] score bg=%u fire=%u knock=%u baby=%u boil=%u => out=%d conf=%u\n",
               (unsigned)prob_to_pct_u8(p[ALARM_TYPE_BACKGROUND]),
               (unsigned)prob_to_pct_u8(p[ALARM_TYPE_FIRE]),
               (unsigned)prob_to_pct_u8(p[ALARM_TYPE_KNOCK]),
               (unsigned)prob_to_pct_u8(p[ALARM_TYPE_BABY]),
               (unsigned)prob_to_pct_u8(p[ALARM_TYPE_BOILING]),
               stable_class,
               (unsigned)prob_to_pct_u8(stable_conf));
}
#endif

static struct rt_messagequeue *s_mq_alarm;
static uint8_t  s_last_mq_type = 0xffu;
static rt_tick_t s_last_mq_tick;

#if EDGI_AI_ENERGY_GATE
static uint16_t s_gate_silence_run;
#endif

#if EDGI_AI_ENERGY_GATE
static uint32_t pcm_mean_sq_u32(const int16_t *pcm, rt_size_t n)
{
    uint64_t acc = 0;

    for (rt_size_t i = 0; i < n; i++)
    {
        int32_t s = pcm[i];
        acc += (uint32_t)(s * s);
    }
    return (uint32_t)(acc / n);
}
#endif

#if EDGI_AI_STABILIZE
static float     s_stab_ema[ALARM_TYPE_MAX];
static rt_bool_t s_stab_ema_inited;
static uint8_t   s_stab_hold; /* 当前稳定输出类别 */
static uint8_t   s_stab_pending;   /* 待确认的非环境类 */
static uint16_t  s_stab_pending_run;
static uint16_t  s_stab_bg_run;
#endif

static void stab_reset(void)
{
#if EDGI_AI_STABILIZE
    s_stab_ema_inited = RT_FALSE;
    s_stab_hold       = (uint8_t)ALARM_TYPE_BACKGROUND;
    s_stab_pending    = (uint8_t)ALARM_TYPE_BACKGROUND;
    s_stab_pending_run = 0;
    s_stab_bg_run      = 0;
#endif
}

/**
 * 输入每帧原始 softmax p[]，输出稳定后的类别与用于 UI 的置信度（同类别下为 EMA 概率）。
 */
static void stab_apply(const float *p, int *out_class, float *out_conf)
{
#if !EDGI_AI_STABILIZE
    int best = 0;
    float bestv = p[0];
    for (int i = 1; i < ALARM_TYPE_MAX; i++)
    {
        if (p[i] > bestv)
        {
            bestv = p[i];
            best = i;
        }
    }
    *out_class = best;
    *out_conf = bestv;
#else
    const float a = EDGI_AI_STAB_EMA_ALPHA;
    if (!s_stab_ema_inited)
    {
        for (int i = 0; i < ALARM_TYPE_MAX; i++)
            s_stab_ema[i] = p[i];
        s_stab_ema_inited = RT_TRUE;
    }
    else
    {
        for (int i = 0; i < ALARM_TYPE_MAX; i++)
            s_stab_ema[i] = a * s_stab_ema[i] + (1.0f - a) * p[i];
    }

    int ema_best = 0;
    float ema_bestv = s_stab_ema[0];
    for (int i = 1; i < ALARM_TYPE_MAX; i++)
    {
        if (s_stab_ema[i] > ema_bestv)
        {
            ema_bestv = s_stab_ema[i];
            ema_best = i;
        }
    }

    int non_bg_best = (int)ALARM_TYPE_FIRE;
    float non_bg_bestv = s_stab_ema[ALARM_TYPE_FIRE];
    for (int i = (int)ALARM_TYPE_FIRE + 1; i < ALARM_TYPE_MAX; i++)
    {
        if (s_stab_ema[i] > non_bg_bestv)
        {
            non_bg_bestv = s_stab_ema[i];
            non_bg_best = i;
        }
    }

    if (ema_best == (int)ALARM_TYPE_BACKGROUND &&
        non_bg_bestv >= EDGI_AI_STAB_MIN_NON_BG_PROB &&
        (ema_bestv - non_bg_bestv) <= EDGI_AI_STAB_BG_MARGIN)
    {
        ema_best = non_bg_best;
        ema_bestv = non_bg_bestv;
    }

    int gated = ema_best;
    if (ema_best != (int)ALARM_TYPE_BACKGROUND &&
        ema_bestv < EDGI_AI_STAB_MIN_NON_BG_PROB)
        gated = (int)ALARM_TYPE_BACKGROUND;

    if (s_stab_hold == (uint8_t)ALARM_TYPE_BACKGROUND)
    {
        s_stab_bg_run = 0;
        if (gated == (int)ALARM_TYPE_BACKGROUND)
        {
            s_stab_pending = (uint8_t)ALARM_TYPE_BACKGROUND;
            s_stab_pending_run = 0;
        }
        else
        {
            if (gated == (int)s_stab_pending)
                s_stab_pending_run++;
            else
            {
                s_stab_pending = (uint8_t)gated;
                s_stab_pending_run = 1u;
            }
            if (s_stab_pending_run >= EDGI_AI_STAB_ENTER_NON_BG_FRAMES)
            {
                s_stab_hold = s_stab_pending;
                s_stab_pending_run = 0;
            }
        }
    }
    else
    {
        if (gated == (int)ALARM_TYPE_BACKGROUND)
        {
            s_stab_pending_run = 0;
            s_stab_bg_run++;
            if (s_stab_bg_run >= EDGI_AI_STAB_RELEASE_BG_FRAMES)
            {
                s_stab_hold = (uint8_t)ALARM_TYPE_BACKGROUND;
                s_stab_bg_run = 0;
            }
        }
        else
        {
            s_stab_bg_run = 0;
            if (gated == (int)s_stab_hold)
            {
                s_stab_pending = (uint8_t)ALARM_TYPE_BACKGROUND;
                s_stab_pending_run = 0;
            }
            else
            {
                if (gated == (int)s_stab_pending)
                    s_stab_pending_run++;
                else
                {
                    s_stab_pending = (uint8_t)gated;
                    s_stab_pending_run = 1u;
                }
                if (s_stab_pending_run >= EDGI_AI_STAB_SWITCH_ALARM_FRAMES)
                {
                    s_stab_hold = s_stab_pending;
                    s_stab_pending_run = 0;
                }
            }
        }
    }

    *out_class = (int)s_stab_hold;
    *out_conf = s_stab_ema[s_stab_hold];
#endif
}

/** 稳定化后再判：须 softmax 足够高且明显压过环境，避免「一点动静就火警 100%」 */
static void edgi_alarm_pick_after_stab(const float *p, int *best, float *bestv)
{
    int b = *best;
    float bv = *bestv;
    float bg = p[ALARM_TYPE_BACKGROUND];

    if (b != (int)ALARM_TYPE_BACKGROUND)
    {
        if (bv < EDGI_AI_ALARM_MIN_PROB ||
            (b == (int)ALARM_TYPE_FIRE && bv < EDGI_AI_FIRE_MIN_PROB) ||
            (b == (int)ALARM_TYPE_KNOCK && bv < EDGI_AI_KNOCK_MIN_PROB) ||
            (b == (int)ALARM_TYPE_BABY && bv < EDGI_AI_BABY_MIN_PROB) ||
            (b == (int)ALARM_TYPE_BOILING && bv < EDGI_AI_BOILING_MIN_PROB) ||
            (bv - bg) < EDGI_AI_ALARM_MIN_MARGIN_BG)
        {
            b = (int)ALARM_TYPE_BACKGROUND;
            bv = bg;
        }
    }
    *best = b;
    *bestv = bv;
}

static float edgi_alarm_min_probability(int alarm_type)
{
    switch (alarm_type)
    {
    case ALARM_TYPE_FIRE:
        return EDGI_AI_FIRE_MIN_PROB;
    case ALARM_TYPE_KNOCK:
        return EDGI_AI_KNOCK_MIN_PROB;
    case ALARM_TYPE_BABY:
        return EDGI_AI_BABY_MIN_PROB;
    case ALARM_TYPE_BOILING:
        return EDGI_AI_BOILING_MIN_PROB;
    default:
        return 1.0f;
    }
}

static void edgi_alarm_gate_probabilities(const float *p, float *gated)
{
    int best = (int)ALARM_TYPE_FIRE;
    float bestv = p[best];
    float runner_up;

    for (int i = 0; i < (int)ALARM_TYPE_MAX; i++)
        gated[i] = p[i];

    for (int i = (int)ALARM_TYPE_FIRE + 1; i < (int)ALARM_TYPE_MAX; i++)
    {
        if (p[i] > bestv)
        {
            best = i;
            bestv = p[i];
        }
    }

    runner_up = p[ALARM_TYPE_BACKGROUND];
    for (int i = (int)ALARM_TYPE_FIRE; i < (int)ALARM_TYPE_MAX; i++)
    {
        if (i != best && p[i] > runner_up)
            runner_up = p[i];
    }

    if (p[ALARM_TYPE_BACKGROUND] >= bestv ||
        bestv < edgi_alarm_min_probability(best) ||
        (bestv - runner_up) < EDGI_AI_ALARM_MIN_MARGIN_NEXT)
    {
        for (int i = 0; i < (int)ALARM_TYPE_MAX; i++)
            gated[i] = 0.0f;
        gated[ALARM_TYPE_BACKGROUND] = 1.0f;
    }
}

#if EDGI_AI_FIELD_TUNING
/**
 * 演示判定：手机外放时 MLP 常 bg≈60%、fire≈20%，全局 argmax 永远 out=0。
 * 在火/敲/婴/水四类中取最高；若 >=12% 则输出该类（bg 仅作环境待机）。
 */
static void edgi_field_tune_pick(const float *p, int *best, float *bestv)
{
    int nbg = (int)ALARM_TYPE_FIRE;
    float nbgv = p[ALARM_TYPE_FIRE];

    for (int i = (int)ALARM_TYPE_FIRE + 1; i < (int)ALARM_TYPE_MAX; i++)
    {
        if (p[i] > nbgv)
        {
            nbgv = p[i];
            nbg = i;
        }
    }
    if (nbgv >= EDGI_AI_FIELD_MIN_ALARM_PROB)
    {
        *best = nbg;
        *bestv = nbgv;
    }
    else
    {
        *best = (int)ALARM_TYPE_BACKGROUND;
        *bestv = p[ALARM_TYPE_BACKGROUND];
    }
}
#endif

#if EDGI_USE_MODEL_STUB || EDGI_AI_USE_DEMO_CENTROID || !EDGI_AI_USE_OPEN_CNN
static void pcm_to_spectrogram_stub(const int16_t *pcm, uint8_t out[40][40])
{
    for (int r = 0; r < 40; r++)
    {
        uint32_t acc = 0;
        int base = (r * (int)EDGI_AUDIO_SAMPLES_PER_FRAME) / 40;
        for (int k = 0; k < 8 && base + k < (int)EDGI_AUDIO_SAMPLES_PER_FRAME; k++)
        {
            int32_t v = pcm[base + k];
            acc += (uint32_t)(v * v);
        }
        uint8_t e = (acc >> EDGI_SPEC_ENERGY_SHIFT) > 255 ? 255 : (uint8_t)(acc >> EDGI_SPEC_ENERGY_SHIFT);
        for (int c = 0; c < 40; c++)
            out[r][c] = e;
    }
}

static void run_model_stub(const uint8_t spec[40][40], float prob[ALARM_TYPE_MAX])
{
    uint32_t sum = 0;
    for (int r = 0; r < 40; r++)
        for (int c = 0; c < 40; c++)
            sum += spec[r][c];
    for (int i = 0; i < ALARM_TYPE_MAX; i++)
        prob[i] = 0.f;
    if (sum < 80000)
        prob[ALARM_TYPE_BACKGROUND] = 0.85f;
    else if (sum < 120000)
        prob[ALARM_TYPE_KNOCK] = 0.7f;
    else if (sum < 180000)
        prob[ALARM_TYPE_FIRE] = 0.75f;
    else
        prob[ALARM_TYPE_BABY] = 0.8f;
}
#endif

static void ai_thread_entry(void *param)
{
    RT_UNUSED(param);
    int16_t pcm[EDGI_AUDIO_SAMPLES_PER_FRAME];
    rt_tick_t startup_tick = rt_tick_get();
    rt_bool_t input_suppressed = RT_FALSE;

#if EDGI_AI_SERIAL_LOG
    rt_kprintf("[AI] thread started\n");
#endif
    stab_reset();
#if EDGI_AI_STABILIZE && EDGI_AI_SERIAL_LOG
    rt_kprintf("[AI] stabilize: EMA enter=%u release=%u\n",
               (unsigned)EDGI_AI_STAB_ENTER_NON_BG_FRAMES,
               (unsigned)EDGI_AI_STAB_RELEASE_BG_FRAMES);
#endif

    rt_uint32_t frame_ok = 0;
    rt_uint32_t rd_fail = 0;

    while (1)
    {
#if EDGI_AI_PAUSE_DURING_WIFI_JOIN
        uint8_t wifi_st = edgi_m33_shm_wifi_state_read();
#endif
        rt_bool_t startup_guard =
            (rt_tick_get() - startup_tick) <
            rt_tick_from_millisecond(EDGI_AI_STARTUP_GUARD_MS);
#if EDGI_AI_PAUSE_DURING_WIFI_JOIN
        rt_bool_t wifi_joining =
            wifi_st == EDGI_WIFI_SHM_DRV ||
            wifi_st == EDGI_WIFI_SHM_JOIN ||
            wifi_st == EDGI_WIFI_SHM_READY ||
            wifi_st == EDGI_WIFI_SHM_AUTO_ARMED ||
            wifi_st == EDGI_WIFI_SHM_AUTO_DELAY ||
            wifi_st == EDGI_WIFI_SHM_STARTING;
#else
        rt_bool_t wifi_joining = RT_FALSE;
#endif

        if (!edgi_m33_service_get_enable())
        {
            input_suppressed = RT_TRUE;
            rt_thread_mdelay(100);
            continue;
        }

        /*
         * Ignore electrical/audio transients while both cores and WiFi power up.
         * Reset the one-second model window once, then resume with fresh audio.
         */
        if (startup_guard || wifi_joining)
        {
            if (!input_suppressed)
            {
#if EDGI_AI_USE_OPEN_CNN
                app_ai_open_cnn_init();
#endif
                stab_reset();
                input_suppressed = RT_TRUE;
            }
            rt_thread_mdelay(wifi_joining ? 25 : 10);
            continue;
        }

        if (input_suppressed)
        {
#if EDGI_AI_USE_OPEN_CNN
            app_ai_open_cnn_init();
#endif
            stab_reset();
            input_suppressed = RT_FALSE;
        }

        rt_ssize_t rd = edgi_audio_capture_read_frame(pcm, EDGI_AUDIO_SAMPLES_PER_FRAME);
        if (rd < 0)
        {
            if (EDGI_AI_SERIAL_LOG && (++rd_fail == 1u || (rd_fail % 5u) == 0u))
                rt_kprintf("[AI] capture read fail(%ld), check mic0/sound0\n", (long)rd);
            rt_thread_mdelay(5);
            continue;
        }
        rd_fail = 0;

        /* 按键暂停采音：read 仍返回静音帧以保持节拍，但不推理、不发 mq，避免 [UI] 刷屏 */
        if (edgi_audio_capture_is_paused())
            continue;

        if (edgi_voice_m33_is_recording())
        {
            (void)edgi_voice_m33_publish_pcm(pcm, EDGI_AUDIO_SAMPLES_PER_FRAME);
            continue;
        }

#if EDGI_AI_ENERGY_GATE
        uint32_t frame_msq = pcm_mean_sq_u32(pcm, EDGI_AUDIO_SAMPLES_PER_FRAME);
#endif

#if EDGI_AI_ENERGY_GATE
        {
            if (frame_msq < EDGI_AI_GATE_MEAN_SQ_MAX)
            {
                s_gate_silence_run++;
                if (s_gate_silence_run == EDGI_AI_GATE_ENTER_FRAMES)
                    stab_reset();
                if (s_gate_silence_run >= EDGI_AI_GATE_ENTER_FRAMES)
                {
                    rt_thread_mdelay(EDGI_AI_SILENCE_YIELD_MS);
                    continue;
                }
            }
            else
                s_gate_silence_run = 0;
        }
#endif

        ++frame_ok;
        if (EDGI_AI_SERIAL_LOG && (frame_ok <= 3u || (frame_ok % 128u) == 0u))
        {
#if EDGI_USE_MODEL_STUB
            rt_kprintf("[AI] frame %lu ok (stub)\n", (unsigned long)frame_ok);
#elif EDGI_AI_USE_OPEN_CNN
            rt_kprintf("[AI] frame %lu ok (open_cnn)\n", (unsigned long)frame_ok);
#else
            rt_kprintf("[AI] frame %lu ok (mlp)\n", (unsigned long)frame_ok);
#endif
        }

        float p[ALARM_TYPE_MAX];
        float gated_p[ALARM_TYPE_MAX];

#if EDGI_USE_MODEL_STUB
        uint8_t spec[40][40];
        pcm_to_spectrogram_stub(pcm, spec);
        run_model_stub(spec, p);
#elif EDGI_AI_USE_OPEN_CNN
        app_ai_open_cnn_push_pcm(pcm, (unsigned)EDGI_AUDIO_SAMPLES_PER_FRAME);
        if (app_ai_open_cnn_infer(p) != 0)
        {
            rt_thread_mdelay(1);
            continue;
        }
#elif EDGI_AI_USE_DEMO_CENTROID
        uint8_t spec[40][40];
        pcm_to_spectrogram_stub(pcm, spec);
        if (app_ai_demo_centroid_infer(spec, p) != 0)
            run_model_stub(spec, p);
#else
        uint8_t spec[40][40];
        pcm_to_spectrogram_stub(pcm, spec);
        if (app_ai_tflm_infer(spec, p) != 0)
            run_model_stub(spec, p);
#endif

        edgi_alarm_gate_probabilities(p, gated_p);

        int best;
        float bestv;
        stab_apply(gated_p, &best, &bestv);
#if EDGI_AI_FIELD_TUNING && !EDGI_AI_USE_DEMO_CENTROID
        edgi_field_tune_pick(gated_p, &best, &bestv);
#else
        edgi_alarm_pick_after_stab(gated_p, &best, &bestv);
#endif

#if EDGI_AI_SCORE_LOG
        ai_score_log(p, best, bestv);
#endif

        rt_tick_t now = rt_tick_get();
        rt_tick_t interval = rt_tick_from_millisecond(EDGI_AI_MQ_INTERVAL_MS);
        rt_bool_t type_new = (s_last_mq_type != (uint8_t)best);
        rt_bool_t time_up  = (now - s_last_mq_tick) >= interval;
#if EDGI_AI_USE_DEMO_CENTROID
        /* 演示模式：仅类别变化才上报，避免 seq 每 200ms 涨导致 LVGL 闪 */
        if (!type_new)
        {
            rt_thread_mdelay(1);
            continue;
        }
#else
        if (!(type_new || time_up))
        {
            rt_thread_mdelay(1);
            continue;
        }
#endif

        AlarmResult ar;
        ar.alarm_type = (uint8_t)best;
        ar.confidence = prob_to_pct_u8(bestv);
        ar._reserved = 0;
        ar.timestamp = now;

        if (s_mq_alarm)
        {
            if (rt_mq_send(s_mq_alarm, &ar, sizeof(AlarmResult)) != RT_EOK)
            {
#if EDGI_AI_SERIAL_LOG
                rt_kprintf("[AI] mq_send failed\n");
#endif
            }
            else
            {
                static rt_uint32_t s_sent_ok;

                s_last_mq_type  = (uint8_t)best;
                s_last_mq_tick  = now;
                if (EDGI_AI_SERIAL_LOG && (++s_sent_ok <= 5u || (s_sent_ok % 32u) == 0u))
                {
                    rt_kprintf("[AI] mq type=%u conf=%u\n",
                               (unsigned)ar.alarm_type,
                               (unsigned)ar.confidence);
                }
            }
        }
        rt_thread_mdelay(1);
    }
}

static rt_uint8_t s_ai_stack[AI_THREAD_STACK];
static struct rt_thread s_ai_thread;

rt_err_t app_ai_start(struct rt_messagequeue *mq_alarm)
{
    s_mq_alarm = mq_alarm;
#if EDGI_AI_FIELD_TUNING || EDGI_AI_USE_DEMO_CENTROID || EDGI_AI_USE_OPEN_CNN
    rt_kprintf("[AI] mic=%u", (unsigned)EDGI_AUDIO_MIC_VOLUME);
#if EDGI_AI_USE_OPEN_CNN
    rt_kprintf(" infer=open_cnn (micro_speech-style log-mel+CNN)\n");
#elif EDGI_AI_USE_DEMO_CENTROID
    rt_kprintf(" infer=demo_cos; 播一类执行 demo_cal N\n");
#else
    rt_kprintf(" infer=mlp+delta_feat softmax\n");
#endif
#endif
#if !EDGI_USE_MODEL_STUB && EDGI_AI_USE_OPEN_CNN
    if (app_ai_open_cnn_init() != 0)
    {
        rt_kprintf("[AI] FAIL: app_ai_open_cnn_init\n");
        return -RT_ERROR;
    }
#elif !EDGI_USE_MODEL_STUB && !EDGI_AI_USE_DEMO_CENTROID && !EDGI_AI_USE_OPEN_CNN
    if (app_ai_tflm_init() != 0)
    {
        rt_kprintf("[AI] FAIL: app_ai_tflm_init\n");
        return -RT_ERROR;
    }
#elif !EDGI_USE_MODEL_STUB && EDGI_AI_USE_DEMO_CENTROID
    if (app_ai_demo_centroid_init() != 0)
    {
        rt_kprintf("[AI] FAIL: app_ai_demo_centroid_init\n");
        return -RT_ERROR;
    }
#endif
    rt_err_t e = edgi_audio_capture_init();
    if (e != RT_EOK)
    {
        rt_kprintf("[AI] FAIL: edgi_audio_capture_init (%d), check mic0\n", (int)e);
        return e;
    }

    e = edgi_voice_m33_init();
    if (e != RT_EOK)
    {
        rt_kprintf("[voice] FAIL: edgi_voice_m33_init (%d)\n", (int)e);
        return e;
    }

    e = rt_thread_init(&s_ai_thread, "ai", ai_thread_entry, RT_NULL,
                       s_ai_stack, sizeof(s_ai_stack), AI_THREAD_PRIO, 10);
    if (e == RT_EOK)
        rt_thread_startup(&s_ai_thread);
    return e;
}
