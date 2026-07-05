#include "../include/app_ui.h"
#include "../include/edgi_alarm_history.h"
#include "../include/edgi_led_green.h"
#include "../include/edgi_m33_m55_shm.h"
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>

#define UI_THREAD_STACK  3072
#define UI_THREAD_PRIO   11

/*
 * whd_res_download 等 YMODEM 传输时置 0，避免 [UI] 周期性刷屏冲坏串口。
 * 传完资源后改回 1 再编译即可恢复串口告警打印。
 */
#ifndef EDGI_UI_SERIAL_LOG
#define EDGI_UI_SERIAL_LOG 0
#endif
/* 1=每 15s 打 [UI] alive（便于确认绿灯/UI 线程未挂） */
#ifndef EDGI_UI_ALIVE_LOG
#define EDGI_UI_ALIVE_LOG 0
#endif

#ifndef EDGI_GREEN_LED_ALIVE_ONLY
#define EDGI_GREEN_LED_ALIVE_ONLY 1
#endif

/* 0=禁止每 2s 无变化仍 publish（否则 seq 一直涨、M55 屏闪） */
#ifndef EDGI_UI_SHM_HEARTBEAT_SEQ
#define EDGI_UI_SHM_HEARTBEAT_SEQ 0
#endif
#ifndef EDGI_UI_SHM_MIN_CONF_DELTA
#define EDGI_UI_SHM_MIN_CONF_DELTA 3u
#endif
#ifndef EDGI_UI_SHM_MIN_UPDATE_MS
#define EDGI_UI_SHM_MIN_UPDATE_MS 1000u
#endif

static struct rt_messagequeue *s_mq_alarm;

static uint8_t ui_abs_u8_delta(uint8_t a, uint8_t b)
{
    return (a > b) ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

static void ui_shm_publish_if_changed(const AlarmResult *ar)
{
    static uint8_t s_pub_type = 0xffu;
    static uint8_t s_pub_conf = 0xffu;
    static rt_tick_t s_last_pub_tick;
    rt_tick_t now = rt_tick_get();
    rt_bool_t first = (s_pub_type == 0xffu);
    rt_bool_t type_changed = (ar->alarm_type != s_pub_type);
    rt_bool_t conf_changed = (ui_abs_u8_delta(ar->confidence, s_pub_conf) >= EDGI_UI_SHM_MIN_CONF_DELTA);
    rt_bool_t time_ok = ((now - s_last_pub_tick) >= rt_tick_from_millisecond(EDGI_UI_SHM_MIN_UPDATE_MS));

    if (!(first || type_changed || (conf_changed && time_ok)))
        return;

    s_pub_type = ar->alarm_type;
    s_pub_conf = ar->confidence;
    s_last_pub_tick = now;
    edgi_m33_m55_shm_publish((const edgi_shm_alarm_result_t *)ar);
}

/*
 * 绿灯 P16_6：仅表示告警灯语（避免与 main.c 里蓝灯 P16_5 抢引脚）。
 * 蓝灯留给 main：采集中亮 / 暂停灭。
 */
static void led_pattern_for_alarm(uint8_t type)
{
    rt_uint32_t t = rt_tick_get();
    rt_bool_t on = RT_FALSE;

#if EDGI_GREEN_LED_ALIVE_ONLY
    RT_UNUSED(type);
    RT_UNUSED(t);
    RT_UNUSED(on);
    return;
#endif

    switch (type)
    {
    case ALARM_TYPE_FIRE:
        on = ((t / (RT_TICK_PER_SECOND / 8)) & 1) ? RT_TRUE : RT_FALSE;
        break;
    case ALARM_TYPE_KNOCK:
        on = ((t / (RT_TICK_PER_SECOND / 2)) & 1) ? RT_TRUE : RT_FALSE;
        break;
    case ALARM_TYPE_BABY:
        on = ((t / (RT_TICK_PER_SECOND / 5)) & 1) ? RT_TRUE : RT_FALSE;
        break;
    case ALARM_TYPE_BOILING:
        on = RT_TRUE;
        break;
    case ALARM_TYPE_BACKGROUND:
    default:
        on = ((t / RT_TICK_PER_SECOND) & 1u) ? RT_TRUE : RT_FALSE;
        break;
    }
    edgi_led_green_set(on);
}

static void ui_show_text_stub(const AlarmResult *r)
{
    static const char *labels[] = {"环境", "火警", "敲门", "婴儿", "水开"};
    const char *name = "未知";
    if (r->alarm_type < sizeof(labels) / sizeof(labels[0]))
        name = labels[r->alarm_type];

#if EDGI_UI_SERIAL_LOG
    rt_kprintf("[UI] %s conf=%u%% tick=%u\n",
               name, (unsigned)r->confidence, (unsigned)r->timestamp);
#else
    RT_UNUSED(name);
#endif
}

static void ui_thread_entry(void *param)
{
    RT_UNUSED(param);
    AlarmResult ar;
    AlarmResult last;

    rt_memset(&last, 0, sizeof(last));
    last.alarm_type = ALARM_TYPE_BACKGROUND;

#if EDGI_UI_SERIAL_LOG
    rt_kprintf("[UI] thread started (serial + green LED alarm)\n");
#endif

    edgi_led_green_blink_start();
    rt_kprintf("[UI] thread started (green P16.6 slow blink = running)\n");

    {
#if EDGI_UI_SHM_HEARTBEAT_SEQ
        rt_tick_t last_hb = rt_tick_get();
#endif
#if EDGI_UI_ALIVE_LOG
        rt_tick_t last_alive = rt_tick_get();
#endif

        while (1)
        {
            edgi_m33_shm_poll_remote_cmds();
#if EDGI_UI_ALIVE_LOG || EDGI_UI_SHM_HEARTBEAT_SEQ
            rt_tick_t now = rt_tick_get();
#endif
#if EDGI_UI_ALIVE_LOG
            if ((now - last_alive) >= rt_tick_from_millisecond(15000))
            {
                rt_kprintf("[UI] alive tick=%u\n", (unsigned)now);
                last_alive = now;
            }
#endif
            /* rt_mq_recv 成功返回消息长度(>0)，不是 RT_EOK */
            rt_ssize_t n = rt_mq_recv(s_mq_alarm, &ar, sizeof(ar),
                                      rt_tick_from_millisecond(50));
            if (n > 0)
            {
                edgi_alarm_history_push(&ar);
                ui_shm_publish_if_changed(&ar);
                ui_show_text_stub(&ar);
                last = ar;
#if EDGI_UI_SHM_HEARTBEAT_SEQ
                last_hb = now;
#endif
            }
#if EDGI_UI_SHM_HEARTBEAT_SEQ
            else if ((now - last_hb) >= rt_tick_from_millisecond(2000))
            {
                ar.alarm_type  = last.alarm_type;
                ar.confidence  = last.confidence;
                ar._reserved   = 0;
                ar.timestamp   = (uint32_t)now;
                ui_shm_publish_if_changed(&ar);
                last_hb = now;
            }
#endif
            led_pattern_for_alarm(last.alarm_type);
        }
    }
}

static rt_uint8_t s_ui_stack[UI_THREAD_STACK];
static struct rt_thread s_ui_thread;

rt_err_t app_ui_start(struct rt_messagequeue *mq_alarm)
{
    s_mq_alarm = mq_alarm;
    rt_err_t e = rt_thread_init(&s_ui_thread, "ui", ui_thread_entry, RT_NULL,
                                s_ui_stack, sizeof(s_ui_stack), UI_THREAD_PRIO, 10);
    if (e == RT_EOK)
        rt_thread_startup(&s_ui_thread);
    return e;
}

#if defined(RT_USING_FINSH) && defined(FINSH_USING_MSH)
#include <finsh.h>
static void cmd_alarm_hist(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    AlarmResult batch[EDGI_ALARM_HISTORY_CAP];
    rt_size_t n = edgi_alarm_history_copy(batch, EDGI_ALARM_HISTORY_CAP);
    static const char *lab[] = {"环境", "火警", "敲门", "婴儿", "水开"};
    rt_kprintf("count=%u\n", (unsigned)n);
    for (rt_size_t i = 0; i < n; i++)
    {
        const char *name = "?";
        if (batch[i].alarm_type < sizeof(lab) / sizeof(lab[0]))
            name = lab[batch[i].alarm_type];
        rt_kprintf("  [%u] %s conf=%u tick=%u\n", (unsigned)i, name,
                   (unsigned)batch[i].confidence, (unsigned)batch[i].timestamp);
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_alarm_hist, alarm_hist, dump alarm history);
#endif
