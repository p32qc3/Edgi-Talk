#ifndef EDGI_LED_GREEN_H
#define EDGI_LED_GREEN_H

#include <rtthread.h>

/* P16.6 慢闪：独立 rt_timer，不依赖 UI 线程（WiFi 后 UI 可能被饿死） */
void edgi_led_green_blink_start(void);
void edgi_led_green_set(rt_bool_t on);

#endif /* EDGI_LED_GREEN_H */
