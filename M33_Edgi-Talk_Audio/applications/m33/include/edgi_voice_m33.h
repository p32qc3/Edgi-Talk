#ifndef EDGI_VOICE_M33_H
#define EDGI_VOICE_M33_H

#include <rtthread.h>
#include <stdint.h>

rt_err_t edgi_voice_m33_init(void);
rt_bool_t edgi_voice_m33_is_recording(void);
rt_err_t edgi_voice_m33_publish_pcm(const int16_t *pcm, rt_size_t samples);

#endif /* EDGI_VOICE_M33_H */
