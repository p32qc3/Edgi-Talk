/*
 * Poll M33->M55 shared alarm (0x261C0000); on new seq, POST to cloud via edgi_alarm_http_post.
 * Requires M33 running Edgi-Talk_Audio and publishing via edgi_m33_m55_shm_publish.
 */
#include <rtthread.h>
#include <rthw.h>
#ifdef RT_USING_FINSH
#include <finsh.h>
#endif
#include <string.h>
#include <stdlib.h>

#include "edgi_m33_m55_shm.h"
#include "edgi_shm_cache.h"
#include "alarm_http.h"

static volatile uint8_t s_m55_wifi_state;
static volatile uint8_t s_m55_ui_state;
static volatile uint8_t s_m55_upload_state;

static void edgi_m55_shm_flush_cmd_line(volatile edgi_m33_m55_shm_t *p)
{
    EDGI_M33_M55_DMB_ISH();
    edgi_shm_cache_flush((uint8_t *)p + EDGI_M33_M55_SHM_M33_DATA_BYTES,
                         (int)EDGI_M33_M55_SHM_CMD_BYTES);
}

static void edgi_m55_shm_write_status(uint8_t field, uint8_t state)
{
    volatile edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    edgi_shm_invalidate_m33_cmd((void *)p);
    if (field == 0u)
    {
        s_m55_wifi_state = state;
        p->m55_wifi_state = state;
    }
    else if (field == 1u)
    {
        s_m55_ui_state = state;
        p->m55_ui_state = state;
    }
    else
    {
        s_m55_upload_state = state;
        p->m55_upload_state = state;
    }
    edgi_m55_shm_flush_cmd_line(p);
    rt_hw_interrupt_enable(level);
}

void edgi_m55_shm_set_wifi_state(uint8_t state)
{
    edgi_m55_shm_write_status(0u, state);
}

void edgi_m55_shm_set_ui_state(uint8_t state)
{
    edgi_m55_shm_write_status(1u, state);
}

void edgi_m55_shm_set_upload_state(uint8_t state)
{
    edgi_m55_shm_write_status(2u, state);
}

void edgi_m55_shm_heartbeat(void)
{
    volatile edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    edgi_shm_invalidate_m33_cmd((void *)p);
    p->m55_wifi_state = s_m55_wifi_state;
    p->m55_ui_state = s_m55_ui_state;
    p->m55_upload_state = s_m55_upload_state;
    p->m55_heartbeat++;
    edgi_m55_shm_flush_cmd_line(p);
    rt_hw_interrupt_enable(level);
}

_Static_assert(EDGI_M33_M55_SHM_M33_DATA_BYTES == 32u,
               "M33 data and M55 commands must use separate cache lines");
_Static_assert(sizeof(edgi_m33_m55_shm_t) == 64u,
               "Shared memory header must stay cache-line aligned");

#ifndef EDGI_ALARM_SHM_POLL_MS
#define EDGI_ALARM_SHM_POLL_MS 1000
#endif

#ifndef EDGI_ALARM_SHM_STACK
#define EDGI_ALARM_SHM_STACK 4096
#endif

#ifndef EDGI_ALARM_SHM_PRIO
#define EDGI_ALARM_SHM_PRIO 15
#endif

#ifndef EDGI_ALARM_SHM_UPLOAD_THREAD
#define EDGI_ALARM_SHM_UPLOAD_THREAD 1
#endif

#ifndef EDGI_ALARM_UPLOAD_SKIP_BACKGROUND
#define EDGI_ALARM_UPLOAD_SKIP_BACKGROUND 1
#endif

#ifndef EDGI_ALARM_UPLOAD_MIN_CONF
/* 联调可改 25；量产建议 35 */
#define EDGI_ALARM_UPLOAD_MIN_CONF 25u
#endif

#ifndef EDGI_ALARM_UPLOAD_COOLDOWN_MS
#define EDGI_ALARM_UPLOAD_COOLDOWN_MS 8000u
#endif

#ifndef EDGI_ALARM_UPLOAD_RETRY_GAP_MS
#define EDGI_ALARM_UPLOAD_RETRY_GAP_MS 8000u
#endif

#ifndef EDGI_ALARM_UPLOAD_SKIP_LOG_GAP_MS
#define EDGI_ALARM_UPLOAD_SKIP_LOG_GAP_MS 2000u
#endif

static uint32_t s_last_uploaded_seq;
static uint8_t s_last_uploaded_type = 0xffu;
static rt_tick_t s_last_upload_tick;
static rt_tick_t s_last_upload_fail_tick;
/* 0=do not auto POST (manual alarm_post still works); 1=auto upload on new M33 seq */
static volatile rt_uint8_t s_cloud_upload_enable = 0;

static int edgi_try_read_stable(edgi_shm_alarm_result_t *out, uint32_t *out_seq);
static int alarm_should_upload(const edgi_shm_alarm_result_t *alarm, uint32_t seq);
static const char *alarm_type_to_code(uint8_t t);
static int confidence_to_level(uint8_t conf);
static int edgi_alarm_do_http_upload(const edgi_shm_alarm_result_t *a, uint32_t seq);
static void edgi_alarm_arm_from_next_seq(void);

void edgi_alarm_set_auto_upload(int enable)
{
    rt_uint8_t old = s_cloud_upload_enable;

    s_cloud_upload_enable = enable ? 1u : 0u;
    edgi_m55_shm_set_upload_state(enable ? EDGI_M55_UPLOAD_AUTO_ON : EDGI_M55_UPLOAD_THREAD);
    if (enable && !old)
        edgi_alarm_arm_from_next_seq();
    rt_kprintf("alarm_upload: auto cloud=%u (M33->HTTP)\n", (unsigned)s_cloud_upload_enable);
}

#ifdef RT_USING_FINSH
static void alarm_upload_cmd(int argc, char **argv)
{
    if (argc >= 2)
    {
        edgi_alarm_set_auto_upload(atoi(argv[1]) != 0);
        return;
    }
    rt_kprintf("alarm_upload <0|1>  — 0=off 1=on (auto POST when M33 seq updates)\n");
    rt_kprintf("测钉钉: alarm_post fire 3  或  alarm_upload 1 后再 cm33 fire\n");
    rt_kprintf("current: auto cloud=%u\n", (unsigned)s_cloud_upload_enable);
}

MSH_CMD_EXPORT_ALIAS(alarm_upload_cmd, alarm_upload, enable/disable auto alarm upload to cloud);
#endif

static int edgi_try_read_stable(edgi_shm_alarm_result_t *out, uint32_t *out_seq)
{
    void *shm_base = (void *)(uintptr_t)EDGI_M33_M55_SHM_SYSRAM_ADDR;
    volatile edgi_m33_m55_shm_t *shm = (volatile edgi_m33_m55_shm_t *)shm_base;
    int attempt;

    for (attempt = 0; attempt < 6; attempt++)
    {
        uint32_t s1;
        uint32_t s2;
        edgi_shm_alarm_result_t a;

        edgi_shm_invalidate_m33_data(shm_base);
        s1 = shm->seq;
        EDGI_M33_M55_DMB_ISH();
        a.alarm_type  = shm->alarm.alarm_type;
        a.confidence  = shm->alarm.confidence;
        a._reserved   = shm->alarm._reserved;
        a.timestamp   = shm->alarm.timestamp;
        EDGI_M33_M55_DMB_ISH();
        s2 = shm->seq;

        if (s1 == s2 && s1 != 0u)
        {
            *out = a;
            *out_seq = s1;
            return 1;
        }
        rt_thread_mdelay(1);
    }
    return 0;
}

static const char *alarm_type_to_code(uint8_t t)
{
    switch (t)
    {
    case 0:
        return "background";
    case 1:
        return "fire";
    case 2:
        return "knock";
    case 3:
        return "baby";
    case 4:
        return "boiling";
    default:
        return "unknown";
    }
}

static int confidence_to_level(uint8_t conf)
{
    if (conf >= 80u)
        return 3;
    if (conf >= 50u)
        return 2;
    return 1;
}

static int alarm_retry_ready(void)
{
    rt_tick_t now = rt_tick_get();
    rt_tick_t gap = rt_tick_from_millisecond(EDGI_ALARM_UPLOAD_RETRY_GAP_MS);

    if (s_last_upload_fail_tick == 0)
        return 1;

    return ((now - s_last_upload_fail_tick) >= gap) ? 1 : 0;
}

static int alarm_skip_log_ready(void)
{
    static rt_tick_t s_last_skip_log;
    rt_tick_t now = rt_tick_get();
    rt_tick_t gap = rt_tick_from_millisecond(EDGI_ALARM_UPLOAD_SKIP_LOG_GAP_MS);

    if ((now - s_last_skip_log) < gap)
        return 0;
    s_last_skip_log = now;
    return 1;
}

static int alarm_should_upload(const edgi_shm_alarm_result_t *alarm, uint32_t seq)
{
    rt_tick_t now = rt_tick_get();
    rt_tick_t cooldown = rt_tick_from_millisecond(EDGI_ALARM_UPLOAD_COOLDOWN_MS);

    if (alarm == RT_NULL)
        return 0;

#if EDGI_ALARM_UPLOAD_SKIP_BACKGROUND
    if (alarm->alarm_type == 0u)
        return 0;
#endif

    if (alarm->alarm_type > 4u)
    {
        if (alarm_skip_log_ready())
        {
            rt_kprintf("alarm_upload: skip seq=%lu invalid type=%u\n",
                       (unsigned long)seq, (unsigned)alarm->alarm_type);
        }
        return 0;
    }

    if (alarm->confidence < EDGI_ALARM_UPLOAD_MIN_CONF)
    {
        if (alarm_skip_log_ready())
        {
            rt_kprintf("alarm_upload: skip seq=%lu type=%u low conf=%u\n",
                       (unsigned long)seq,
                       (unsigned)alarm->alarm_type,
                       (unsigned)alarm->confidence);
        }
        return 0;
    }

    if (s_last_uploaded_type == alarm->alarm_type &&
        (now - s_last_upload_tick) < cooldown)
    {
        return 0;
    }

    return 1;
}

static int edgi_alarm_do_http_upload(const edgi_shm_alarm_result_t *a, uint32_t seq)
{
    edgi_m55_shm_set_upload_state(EDGI_M55_UPLOAD_POSTING);
    if (edgi_alarm_http_post(alarm_type_to_code(a->alarm_type), confidence_to_level(a->confidence)) != 0)
    {
        edgi_m55_shm_set_upload_state(EDGI_M55_UPLOAD_FAIL);
        rt_kprintf("alarm_upload: HTTP fail seq=%lu type=%u conf=%u (试 alarm_post fire 3)\n",
                   (unsigned long)seq,
                   (unsigned)a->alarm_type,
                   (unsigned)a->confidence);
        s_last_upload_fail_tick = rt_tick_get();
        return -1;
    }

    edgi_m55_shm_set_upload_state(EDGI_M55_UPLOAD_OK);
    rt_kprintf("alarm_upload: HTTP ok seq=%lu type=%u -> configured ECS\n",
               (unsigned long)seq, (unsigned)a->alarm_type);
    s_last_upload_fail_tick = 0;
    s_last_uploaded_type = a->alarm_type;
    s_last_upload_tick = rt_tick_get();
    return 0;
}

static void edgi_alarm_arm_from_next_seq(void)
{
    edgi_shm_alarm_result_t a;
    uint32_t seq;

    if (!edgi_try_read_stable(&a, &seq))
    {
        s_last_uploaded_seq = 0;
        rt_kprintf("alarm_upload: armed, baseline unavailable\n");
        return;
    }

    s_last_uploaded_seq = seq;
    s_last_upload_fail_tick = 0;

    if (alarm_should_upload(&a, seq) && alarm_retry_ready())
    {
        if (edgi_alarm_do_http_upload(&a, seq) != 0)
            s_last_uploaded_seq = (seq > 0u) ? (seq - 1u) : 0u;
        return;
    }

    rt_kprintf("alarm_upload: baseline seq=%lu type=%u (背景/低置信度不上云)\n",
               (unsigned long)seq, (unsigned)a.alarm_type);
    rt_kprintf("alarm_upload: 开上云后请再 cm33 fire 或 alarm_post fire 3\n");
}

#if EDGI_ALARM_SHM_UPLOAD_THREAD
static void alarm_shm_upload_thread(void *param)
{
    RT_UNUSED(param);
    edgi_m55_shm_set_upload_state(EDGI_M55_UPLOAD_THREAD);

    while (1)
    {
        void *shm_base = (void *)(uintptr_t)EDGI_M33_M55_SHM_SYSRAM_ADDR;

        if (!s_cloud_upload_enable)
        {
            rt_thread_mdelay(EDGI_ALARM_SHM_POLL_MS);
            continue;
        }

        {
            volatile edgi_m33_m55_shm_t *ps = edgi_m33_m55_shm_get();

            edgi_shm_cache_invalidate((void *)&ps->m55_wifi_state, (int)sizeof(ps->m55_wifi_state));
            if (ps->m55_wifi_state == EDGI_WIFI_SHM_JOIN)
            {
                rt_thread_mdelay(500);
                continue;
            }
        }

        edgi_shm_invalidate_m33_data(shm_base);
        if (!edgi_shm_service_enabled())
        {
            rt_thread_mdelay(EDGI_ALARM_SHM_POLL_MS);
            continue;
        }

        edgi_shm_alarm_result_t a;
        uint32_t seq;

        if (edgi_try_read_stable(&a, &seq) && seq != 0 && seq != s_last_uploaded_seq)
        {
            if (!alarm_should_upload(&a, seq))
            {
                s_last_uploaded_seq = seq;
                rt_thread_mdelay(EDGI_ALARM_SHM_POLL_MS);
                continue;
            }
            if (!alarm_retry_ready())
            {
                rt_thread_mdelay(EDGI_ALARM_SHM_POLL_MS);
                continue;
            }
            s_last_uploaded_seq = seq;
            if (edgi_alarm_do_http_upload(&a, seq) != 0)
                s_last_uploaded_seq = (seq > 0u) ? (seq - 1u) : 0u;
        }
        rt_thread_mdelay(s_cloud_upload_enable ? 2000 : EDGI_ALARM_SHM_POLL_MS);
    }
}
#endif

#ifdef RT_USING_FINSH
static uint32_t s_peek_last_seq;
static uint32_t s_peek_last_ts;

static void m33_peek_print_result(const edgi_shm_alarm_result_t *a, uint32_t seq)
{
    rt_kprintf("M33 shm: seq=%lu type=%u conf=%u ts=%lu\n",
               (unsigned long)seq,
               (unsigned)a->alarm_type,
               (unsigned)a->confidence,
               (unsigned long)a->timestamp);
    if (seq <= 1u)
        rt_kprintf("  hint: seq 不变请确认 Audio/M55 都烧了新版；M33 有识别结果时 seq 应增加\n");
    else
        rt_kprintf("  hint: 连按两次若 seq/ts 相同 -> M33 当前没有发布新识别结果\n");
}

static void m33_peek(int argc, char **argv)
{
    edgi_shm_alarm_result_t a;
    uint32_t seq;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (!edgi_try_read_stable(&a, &seq))
    {
        rt_kprintf("M33 shm: read unstable (M33 可能未写 0x261C0000)\n");
        return;
    }
    m33_peek_print_result(&a, seq);
    if (seq == s_peek_last_seq && a.timestamp == s_peek_last_ts)
        rt_kprintf("  *** seq/ts 未变 — M33 当前没有发布新识别结果，请看 Audio 串口识别输出 ***\n");
    s_peek_last_seq = seq;
    s_peek_last_ts  = a.timestamp;
}

MSH_CMD_EXPORT(m33_peek, Read M33 alarm shared memory from M55 shell);

static uint8_t edgi_cm33_parse_op(const char *s)
{
    if (s == RT_NULL)
        return EDGI_SHM_CMD_NONE;
    if (!rt_strcmp(s, "stat"))
        return EDGI_SHM_CMD_M33_STAT;
    if (!rt_strcmp(s, "keydbg") || !rt_strcmp(s, "key_dbg"))
        return EDGI_SHM_CMD_KEY_DBG;
    if (!rt_strcmp(s, "keyrec") || !rt_strcmp(s, "key_rec"))
        return EDGI_SHM_CMD_KEY_REC;
    if (!rt_strcmp(s, "fire") || !rt_strcmp(s, "testfire") || !rt_strcmp(s, "test_fire"))
        return EDGI_SHM_CMD_TEST_FIRE;
    return EDGI_SHM_CMD_NONE;
}

static void cm33(int argc, char **argv)
{
    volatile edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();
    uint8_t op;
    uint32_t new_seq;
    int wait;

    if (argc < 2)
    {
        rt_kprintf("cm33 stat|keydbg|keyrec|fire\n");
        rt_kprintf("  cm33 fire = ask M33 to publish one fire alarm for full-chain test\n");
        return;
    }

    op = edgi_cm33_parse_op(argv[1]);
    if (op == EDGI_SHM_CMD_NONE)
    {
        rt_kprintf("cm33: unknown '%s' (use stat|keydbg|keyrec|fire)\n", argv[1]);
        return;
    }

    edgi_shm_invalidate_m33_cmd((void *)p);
    p->m55_cmd_op = op;
    EDGI_M33_M55_DMB_ISH();
    new_seq = p->m55_cmd_seq + 1u;
    p->m55_cmd_seq = new_seq;
    edgi_shm_cache_flush((uint8_t *)p + EDGI_M33_M55_SHM_M33_DATA_BYTES,
                         (int)EDGI_M33_M55_SHM_CMD_BYTES);

    rt_kprintf("cm33: sent op=%u cmd_seq=%lu — 等 M33 回 [CM33] (最多 5s)\n",
               (unsigned)op, (unsigned long)new_seq);

    for (wait = 0; wait < 50; wait++)
    {
        edgi_shm_invalidate_m33_cmd((void *)p);
        if (p->m33_cmd_done == new_seq)
        {
            rt_kprintf("cm33: M33 ack ok (m33_cmd_done=%lu)\n", (unsigned long)p->m33_cmd_done);
            return;
        }
        rt_thread_mdelay(100);
    }
    rt_kprintf("cm33: no M33 ack in 5s (M33 挂死? 查 M55 是否误开 i2c0 / 重烧双核)\n");
}

MSH_CMD_EXPORT(cm33, Proxy M33 shell via shared memory (stat keydbg keyrec fire));

static void edgi_m55_send_m33_cmd(uint8_t op)
{
    volatile edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();
    uint32_t new_seq;
    int wait;

    edgi_shm_invalidate_m33_cmd((void *)p);
    p->m55_cmd_op = op;
    EDGI_M33_M55_DMB_ISH();
    new_seq = p->m55_cmd_seq + 1u;
    p->m55_cmd_seq = new_seq;
    edgi_shm_cache_flush((uint8_t *)p + EDGI_M33_M55_SHM_M33_DATA_BYTES,
                         (int)EDGI_M33_M55_SHM_CMD_BYTES);

    for (wait = 0; wait < 50; wait++)
    {
        edgi_shm_invalidate_m33_cmd((void *)p);
        if (p->m33_cmd_done == new_seq)
            return;
        rt_thread_mdelay(100);
    }
    rt_kprintf("demo_cal: M33 未应答 (5s)，查 M33 是否运行\n");
}

static void demo_cal_m55(int argc, char **argv)
{
    int lab;
    uint8_t op;

    if (argc >= 2 && !rt_strcmp(argv[1], "stat"))
    {
        edgi_m55_send_m33_cmd(EDGI_SHM_CMD_M33_STAT);
        rt_kprintf("demo_cal stat: 看上方 [CM33] demo_cal mask\n");
        return;
    }

    if (argc < 2)
    {
        rt_kprintf("demo_cal <1|2|3|4>  — 播对应声音时在 M55 敲（转发 M33）\n");
        rt_kprintf("  1火警 2敲门 3婴儿 4水开 | demo_cal stat\n");
        return;
    }

    lab = atoi(argv[1]);
    if (lab < 1 || lab > 4)
    {
        rt_kprintf("demo_cal: 类别须 1~4\n");
        return;
    }

    op = (uint8_t)(EDGI_SHM_CMD_DEMO_CAL_1 + lab - 1);
    rt_kprintf("demo_cal: 转发 M33 存模板 class=%d (正在播该类声音?)\n", lab);
    edgi_m55_send_m33_cmd(op);
}

MSH_CMD_EXPORT_ALIAS(demo_cal_m55, demo_cal, Calibrate M33 demo sound via M55 shell);

static void help_edgi(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    rt_kprintf(">>> 当前是 M55 shell (msh />) <<<\n");
    rt_kprintf("  wifi_start  wifi_join_edgi [ssid pwd]  — 少打字避免串口乱码\n");
    rt_kprintf("  m33_peek  demo_cal 1~4  cm33 stat|keydbg|keyrec|fire  alarm_upload\n");
    rt_kprintf("  手敲 wifi join 若变 wf/fji：M33 已让出 uart2，等日志停 1s 再输入一行\n");
    rt_kprintf("  cm33 = 在 M55 上远程触发 M33 打印( uart2 混流, 搜 [CM33] )\n");
    rt_kprintf(">>> 直连 M33 时: key_dbg key_stat m33_stat cm55_stat <<<\n");
    rt_kprintf("钉钉: alarm_upload 1 后 cm33 fire；或直接 alarm_post fire 3\n");
    rt_kprintf("无 alarm_post/server response: 查 ECS 上 DINGTALK_WEBHOOK 是否配置\n");
    rt_kprintf("若 seq 不变: cm33 stat / 看 Audio 串口识别输出\n");
}
MSH_CMD_EXPORT(help_edgi, Edgi dual-core command cheat sheet);
#endif

#if EDGI_ALARM_SHM_UPLOAD_THREAD
static int alarm_shm_upload_init(void)
{
    rt_thread_t t = rt_thread_create("alm_up",
                                     alarm_shm_upload_thread,
                                     RT_NULL,
                                     EDGI_ALARM_SHM_STACK,
                                     EDGI_ALARM_SHM_PRIO,
                                     20);
    if (t == RT_NULL)
        return -1;
    rt_thread_startup(t);
    return 0;
}

INIT_APP_EXPORT(alarm_shm_upload_init);
#endif
