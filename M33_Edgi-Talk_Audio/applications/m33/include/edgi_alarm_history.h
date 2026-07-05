#ifndef EDGI_ALARM_HISTORY_H
#define EDGI_ALARM_HISTORY_H

#include "protocol.h"
#include <rtthread.h>

#define EDGI_ALARM_HISTORY_CAP  16u

void edgi_alarm_history_init(void);
void edgi_alarm_history_push(const AlarmResult *r);
rt_size_t edgi_alarm_history_copy(AlarmResult *out, rt_size_t max_out);
void edgi_alarm_history_clear(void);

#endif
