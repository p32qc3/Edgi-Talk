#ifndef EDGI_KEY_BUTTON_H
#define EDGI_KEY_BUTTON_H

#include <rtthread.h>

void edgi_key_button_init(void);
void edgi_key_gpio_reinit(void);
rt_bool_t edgi_key_recording_enabled(void);

/* M55 经共享内存触发（cm33 keydbg/keyrec） */
void edgi_key_shm_remote_exec(uint8_t op);

#endif
