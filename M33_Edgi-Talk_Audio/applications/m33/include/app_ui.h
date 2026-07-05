#ifndef APP_UI_H
#define APP_UI_H

#include <rtthread.h>
#include "protocol.h"

/**
 * 启动 UI 线程：从 mq_alarm 取结果 -> RGB LED / 屏幕 / 振动
 */
rt_err_t app_ui_start(struct rt_messagequeue *mq_alarm);

#endif /* APP_UI_H */
