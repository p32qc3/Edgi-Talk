#ifndef VOICE_HTTP_H
#define VOICE_HTTP_H

#include <stddef.h>
#include <stdint.h>

#include "voice_response.h"

typedef enum
{
    VOICE_HTTP_OK = 0,
    VOICE_HTTP_OFFLINE = -1,
    VOICE_HTTP_CONNECT = -2,
    VOICE_HTTP_PROTOCOL = -3,
    VOICE_HTTP_REMOTE = -4,
    VOICE_HTTP_TIMEOUT = -5,
    VOICE_HTTP_TOO_LARGE = -6
} VoiceHttpResult;

typedef int (*VoiceHttpAudioSink)(const uint8_t *data, size_t size,
                                  void *context);

int voice_http_post_turn(const uint8_t *pcm, size_t pcm_bytes,
                         const char *turn_id, VoiceResponse *response);
int voice_http_get_audio(const char *audio_id, VoiceHttpAudioSink sink,
                         void *context, size_t *received_bytes);

#endif /* VOICE_HTTP_H */
