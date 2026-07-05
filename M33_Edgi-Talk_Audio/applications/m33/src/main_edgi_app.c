/**
 * 集成说明：源文件在 src/，头文件在 include/；本目录使用 ../include/ 相对包含。
 * “UI”当前为：串口 rt_kprintf + 绿灯 P16_6 灯语；蓝灯 P16_5 仅由 main.c（暂停键）控制。
 */
#include <rtthread.h>
#include "../include/app_ai.h"
#include "../include/app_ui.h"
#include "../include/edgi_alarm_history.h"
#include "../include/edgi_led_green.h"
#include "../include/edgi_m33_m55_shm.h"
#include "../include/protocol.h"

/* 1=打印 [edgi]/[AI] 等详细日志；0=仅打印启动成败（建议保持 1 联调） */
#ifndef EDGI_APP_SERIAL_LOG
#define EDGI_APP_SERIAL_LOG 1
#endif

#define ALARM_MQ_MSG_SIZE   sizeof(AlarmResult)
#define ALARM_MQ_MAX_MSG    8

static struct rt_messagequeue s_alarm_mq;
static rt_uint8_t s_alarm_mq_pool[ALARM_MQ_MSG_SIZE * ALARM_MQ_MAX_MSG];

static int edgi_alarm_app_init(void)
{
    rt_err_t e;

    rt_kprintf("[edgi] M33 alarm app init...\n");

    edgi_m33_m55_shm_init();
    edgi_led_green_blink_start();
    edgi_alarm_history_init();

    if (rt_mq_init(&s_alarm_mq, "alarm_mq", s_alarm_mq_pool,
                   ALARM_MQ_MSG_SIZE, sizeof(s_alarm_mq_pool),
                   RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        rt_kprintf("[edgi] FAIL: rt_mq_init\n");
        return -1;
    }

    e = app_ui_start(&s_alarm_mq);
    if (e != RT_EOK)
    {
        rt_kprintf("[edgi] FAIL: app_ui_start (%d)\n", (int)e);
        return -1;
    }

#ifdef EDGI_M33_BRINGUP_NO_AI
    rt_kprintf("[edgi] BRINGUP: AI 未启动 (EDGI_M33_BRINGUP_NO_AI)，仅 UI/shm 心跳\n");
#else
    e = app_ai_start(&s_alarm_mq);
    if (e != RT_EOK)
    {
        rt_kprintf("[edgi] FAIL: app_ai_start (%d) — 屏上 seq/置信度/时间戳不会更新\n", (int)e);
        return -1;
    }
#endif

    rt_kprintf("[edgi] OK: ui+ai started (M55 屏靠 0x261C0000 seq 刷新)\n");
    return 0;
}
INIT_APP_EXPORT(edgi_alarm_app_init);
