#include "../include/edgi_alarm_history.h"
#include <rtthread.h>
#include <string.h>

static AlarmResult s_buf[EDGI_ALARM_HISTORY_CAP];
static rt_uint32_t s_count;

void edgi_alarm_history_init(void)
{
    rt_enter_critical();
    s_count = 0;
    rt_memset(s_buf, 0, sizeof(s_buf));
    rt_exit_critical();
}

void edgi_alarm_history_push(const AlarmResult *r)
{
    if (r == RT_NULL)
        return;

    rt_enter_critical();
    if (s_count < EDGI_ALARM_HISTORY_CAP)
    {
        s_buf[s_count] = *r;
        s_count++;
    }
    else
    {
        rt_memmove(&s_buf[0], &s_buf[1],
                   (EDGI_ALARM_HISTORY_CAP - 1u) * sizeof(AlarmResult));
        s_buf[EDGI_ALARM_HISTORY_CAP - 1u] = *r;
    }
    rt_exit_critical();
}

rt_size_t edgi_alarm_history_copy(AlarmResult *out, rt_size_t max_out)
{
    if (out == RT_NULL || max_out == 0)
        return 0;

    rt_enter_critical();
    rt_size_t n = s_count < max_out ? s_count : max_out;
    if (n > 0)
    {
        const AlarmResult *src = &s_buf[s_count - n];
        rt_memcpy(out, src, n * sizeof(AlarmResult));
    }
    rt_exit_critical();
    return n;
}

void edgi_alarm_history_clear(void)
{
    rt_enter_critical();
    s_count = 0;
    rt_exit_critical();
}

/* Finsh 命令在 app_ui.c 注册 */
