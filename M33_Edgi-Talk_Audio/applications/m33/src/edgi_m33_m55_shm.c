/**
 * M33：写 m33_m55_shared 首址上的 edgi_m33_m55_shm_t（与 M55 共用物理 0x261C0000）。
 */
#include "../include/edgi_m33_m55_shm.h"
#include "../include/edgi_shm_cache.h"
#include "../include/edgi_key_button.h"
#include "../include/app_ai.h"
#include "../include/protocol.h"
#include <rtthread.h>
#include <string.h>
#include <stdlib.h>

_Static_assert(sizeof(AlarmResult) == sizeof(edgi_shm_alarm_result_t),
               "AlarmResult must match edgi_shm_alarm_result_t");
_Static_assert(EDGI_M33_M55_SHM_M33_DATA_BYTES == 32u,
               "M33 data and M55 commands must use separate cache lines");
_Static_assert(sizeof(edgi_m33_m55_shm_t) == 64u,
               "Shared memory header must stay cache-line aligned");

static volatile rt_uint8_t s_hb_running;
static volatile rt_uint8_t s_m33_wifi_state_cached;
static struct rt_thread s_hb_thread;
static rt_uint8_t s_hb_stack[1024];

#ifndef EDGI_SHM_HB_LOG_EVERY
#define EDGI_SHM_HB_LOG_EVERY  0u
#endif
#ifndef EDGI_SHM_HEARTBEAT_PUBLISH
#define EDGI_SHM_HEARTBEAT_PUBLISH 0
#endif

static int edgi_shm_heartbeat_start(void);
static uint32_t s_handled_cmd_seq;
static struct rt_timer s_cmd_timer;

void edgi_m33_m55_shm_init(void)
{
    edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();
    edgi_shm_alarm_result_t boot;

    /*
     * Preserve M55 status when M33 restarts, but reset the command handshake
     * so retained SRAM cannot replay an old or random command.
     */
    edgi_shm_invalidate_m33_cmd(p);
    s_m33_wifi_state_cached = p->m55_wifi_state;
    p->m55_cmd_seq = 0u;
    p->m33_cmd_done = 0u;
    p->m55_cmd_op = EDGI_SHM_CMD_NONE;
    EDGI_M33_M55_DMB_ISH();
    edgi_shm_cache_flush((uint8_t *)p + EDGI_M33_M55_SHM_M33_DATA_BYTES,
                         (int)EDGI_M33_M55_SHM_CMD_BYTES);
    s_handled_cmd_seq = 0u;
    rt_memset(p, 0, EDGI_M33_M55_SHM_M33_DATA_BYTES);
    p->service_enable = 1u;

    boot.alarm_type  = ALARM_TYPE_BACKGROUND;
    boot.confidence  = 0u;
    boot._reserved   = 0u;
    boot.timestamp   = (uint32_t)rt_tick_get();

    edgi_m33_m55_shm_publish(&boot);
    edgi_shm_heartbeat_start();
    rt_kprintf("[shm] init @0x%08X seq=%lu (M55 屏读此区)\n",
               (unsigned)EDGI_M33_M55_SHM_SYSRAM_ADDR, (unsigned long)p->seq);
}

void edgi_m33_m55_shm_publish(const edgi_shm_alarm_result_t *alarm)
{
    const AlarmResult *src = (const AlarmResult *)alarm;
    edgi_m33_m55_shm_t *p  = edgi_m33_m55_shm_get();

    if (alarm == RT_NULL)
        return;

    p->alarm.alarm_type  = src->alarm_type;
    p->alarm.confidence  = src->confidence;
    p->alarm._reserved   = src->_reserved;
    p->alarm.timestamp   = src->timestamp;

    EDGI_M33_M55_DMB_ISH();
    p->seq++;
    EDGI_M33_M55_COARSE_BARRIER();

    edgi_shm_cache_flush((void *)p, (int)EDGI_M33_M55_SHM_M33_DATA_BYTES);
}

static void edgi_m33_shm_print_stat(void)
{
    edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();
    uint32_t seq;
    edgi_shm_alarm_result_t alarm;
    uint8_t svc;
    uint8_t wifi;

    edgi_shm_cache_invalidate((void *)p, (int)EDGI_M33_M55_SHM_M33_DATA_BYTES);
    edgi_shm_invalidate_m33_cmd((void *)p);
    seq = p->seq;
    alarm = p->alarm;
    svc = p->service_enable;
    wifi = p->m55_wifi_state;

    rt_kprintf("M33 shm @0x%08X: seq=%lu type=%u conf=%u ts=%lu svc=%u wifi=%u",
               (unsigned)EDGI_M33_M55_SHM_SYSRAM_ADDR,
               (unsigned long)seq,
               (unsigned)alarm.alarm_type,
               (unsigned)alarm.confidence,
               (unsigned long)alarm.timestamp,
               (unsigned)svc,
               (unsigned)wifi);
    if (seq == 0u)
        rt_kprintf(" (未发布? 等 [shm] init 或 AI/UI)");
    rt_kprintf("\n");
}

uint8_t edgi_m33_shm_wifi_state_read(void)
{
    return s_m33_wifi_state_cached;
}

static void edgi_m33_shm_wifi_state_refresh(void)
{
    edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();

    edgi_shm_cache_invalidate((void *)&p->m55_wifi_state, (int)sizeof(p->m55_wifi_state));
    EDGI_M33_M55_DMB_ISH();
    s_m33_wifi_state_cached = p->m55_wifi_state;
}

void edgi_m33_shm_poll_remote_cmds(void)
{
    edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();
    uint32_t cmd_seq;

        edgi_shm_invalidate_m33_cmd(p);
        cmd_seq = p->m55_cmd_seq;
    if (cmd_seq == 0u || cmd_seq == s_handled_cmd_seq)
        return;
    if (cmd_seq == p->m33_cmd_done)
    {
        s_handled_cmd_seq = cmd_seq;
        return;
    }

    rt_kprintf("\r\n[CM33] shm cmd seq=%lu op=%u\r\n",
               (unsigned long)cmd_seq, (unsigned)p->m55_cmd_op);

    switch (p->m55_cmd_op)
    {
    case EDGI_SHM_CMD_M33_STAT:
        edgi_m33_shm_print_stat();
        break;
    case EDGI_SHM_CMD_KEY_DBG:
    case EDGI_SHM_CMD_KEY_REC:
        edgi_key_shm_remote_exec(p->m55_cmd_op);
        break;
    case EDGI_SHM_CMD_TEST_FIRE:
    {
        edgi_shm_alarm_result_t ar;

        ar.alarm_type = ALARM_TYPE_FIRE;
        ar.confidence = 88u;
        ar._reserved = 0u;
        ar.timestamp = (uint32_t)rt_tick_get();
        edgi_m33_m55_shm_publish(&ar);
        rt_kprintf("[CM33] test alarm: fire conf=%u seq=%lu\r\n",
                   (unsigned)ar.confidence,
                   (unsigned long)edgi_m33_m55_shm_get()->seq);
        break;
    }
    case EDGI_SHM_CMD_DEMO_CAL_1:
    case EDGI_SHM_CMD_DEMO_CAL_2:
    case EDGI_SHM_CMD_DEMO_CAL_3:
    case EDGI_SHM_CMD_DEMO_CAL_4:
        edgi_demo_cal_store((uint8_t)(p->m55_cmd_op - EDGI_SHM_CMD_DEMO_CAL_1 + 1u));
        break;
    default:
        rt_kprintf("[CM33] shm cmd: unknown op %u\r\n", (unsigned)p->m55_cmd_op);
        break;
    }

    p->m33_cmd_done = cmd_seq;
    EDGI_M33_M55_DMB_ISH();
    edgi_shm_cache_flush((uint8_t *)p + EDGI_M33_M55_SHM_M33_DATA_BYTES,
                         (int)(sizeof(*p) - EDGI_M33_M55_SHM_M33_DATA_BYTES));
    s_handled_cmd_seq = cmd_seq;
}

static void edgi_shm_cmd_timer_cb(void *param)
{
    (void)param;
    edgi_m33_shm_wifi_state_refresh();
    /* Commands can sleep (for example KEY diagnostics), so the heartbeat
     * thread handles them instead of this timer callback. */
}

static void edgi_shm_heartbeat_entry(void *param)
{
    edgi_shm_alarm_result_t hb;
    uint32_t n = 0;
    uint32_t last_seq = 0;
    uint32_t stall_s = 0;

    (void)param;
    rt_memset(&hb, 0, sizeof(hb));
    hb.alarm_type = ALARM_TYPE_BACKGROUND;

    while (s_hb_running)
    {
        edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();

        edgi_m33_shm_wifi_state_refresh();

        {
            uint8_t ws = s_m33_wifi_state_cached;

            if (ws >= EDGI_WIFI_SHM_DRV && ws < EDGI_WIFI_SHM_STABLE)
            {
                rt_thread_mdelay(1000);
                continue;
            }
        }

        hb.confidence = p->alarm.confidence;
        hb.alarm_type = p->alarm.alarm_type;
        hb.timestamp  = (uint32_t)rt_tick_get();
#if EDGI_SHM_HEARTBEAT_PUBLISH
        edgi_m33_m55_shm_publish(&hb);
#endif
        n++;

#if EDGI_SHM_HEARTBEAT_PUBLISH
        if (p->seq == last_seq)
        {
            stall_s++;
            if (stall_s == 5u)
            {
                rt_kprintf("\r\n[CM33] WARN shm seq 5s 未涨 (seq=%lu) — M55 勿动 P16/codec GPIO\r\n",
                           (unsigned long)p->seq);
            }
        }
        else
        {
            last_seq = p->seq;
            stall_s = 0;
        }
#else
        RT_UNUSED(last_seq);
        RT_UNUSED(stall_s);
#endif

#if EDGI_SHM_HB_LOG_EVERY
        if (n <= 3u || (n % EDGI_SHM_HB_LOG_EVERY) == 0u)
        {
            rt_kprintf("\r\n[CM33] shm hb #%lu seq=%lu wifi=%u\r\n",
                       (unsigned long)n, (unsigned long)p->seq,
                       (unsigned)s_m33_wifi_state_cached);
        }
#endif
        rt_thread_mdelay(1000);
    }
}

static int edgi_shm_heartbeat_start(void)
{
    rt_err_t e;

    s_hb_running = 1;
    e = rt_thread_init(&s_hb_thread, "shm_hb", edgi_shm_heartbeat_entry, RT_NULL,
                       s_hb_stack, sizeof(s_hb_stack), 8, 10);
    if (e != RT_EOK)
    {
        rt_kprintf("[shm] heartbeat thread FAIL (%d)\n", (int)e);
        return -1;
    }
    rt_thread_startup(&s_hb_thread);

    rt_timer_init(&s_cmd_timer, "shm_cmd", edgi_shm_cmd_timer_cb, RT_NULL,
                  rt_tick_from_millisecond(200), RT_TIMER_FLAG_PERIODIC);
    rt_timer_start(&s_cmd_timer);

    rt_kprintf("[shm] heartbeat 1s + wifi poll 200ms\n");
    return 0;
}

void edgi_m33_service_set_enable(uint8_t enable)
{
    edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();

    p->service_enable = enable ? 1u : 0u;
    EDGI_M33_M55_DMB_ISH();
    edgi_shm_cache_flush((void *)&p->service_enable, (int)sizeof(p->service_enable));
}

uint8_t edgi_m33_service_get_enable(void)
{
    edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();

    return p->service_enable ? 1u : 0u;
}

#ifdef RT_USING_FINSH
#include <finsh.h>

static void m33_stat(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    edgi_m33_shm_print_stat();
}

static void m33_pub_test(int argc, char **argv)
{
    edgi_shm_alarm_result_t ar;
    uint32_t before;
    int type = ALARM_TYPE_FIRE;
    int conf = 88;

    if (argc >= 2)
        type = atoi(argv[1]);
    if (argc >= 3)
        conf = atoi(argv[2]);
    if (type < ALARM_TYPE_BACKGROUND || type >= ALARM_TYPE_MAX)
        type = ALARM_TYPE_FIRE;
    if (conf < 0)
        conf = 0;
    if (conf > 100)
        conf = 100;

    before = edgi_m33_m55_shm_get()->seq;
    ar.alarm_type  = (uint8_t)type;
    ar.confidence  = (uint8_t)conf;
    ar._reserved   = 0u;
    ar.timestamp   = (uint32_t)rt_tick_get();
    edgi_m33_m55_shm_publish(&ar);
    rt_kprintf("m33_pub_test: type=%u conf=%u seq %lu -> %lu\n",
               (unsigned)ar.alarm_type,
               (unsigned)ar.confidence,
               (unsigned long)before,
               (unsigned long)edgi_m33_m55_shm_get()->seq);
}

static void m33_fire(int argc, char **argv)
{
    char *args[3];

    args[0] = "m33_pub_test";
    args[1] = "1";
    args[2] = (argc >= 2) ? argv[1] : "88";
    m33_pub_test(3, args);
}

MSH_CMD_EXPORT(m33_stat, M33 shared alarm status (seq must increase));
MSH_CMD_EXPORT(m33_pub_test, M33: publish test alarm to shm: m33_pub_test [type] [conf]);
MSH_CMD_EXPORT(m33_fire, M33: publish one fire alarm test frame: m33_fire [conf]);
#endif
