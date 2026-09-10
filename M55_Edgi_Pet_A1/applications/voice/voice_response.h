#ifndef VOICE_RESPONSE_H
#define VOICE_RESPONSE_H

#include <stddef.h>
#include <stdint.h>

#include "voice_session.h"

#define VOICE_RESPONSE_JSON_MAX       768u
#define VOICE_TURN_ID_MAX             65u
#define VOICE_TRANSCRIPT_MAX          192u
#define VOICE_REPLY_TEXT_MAX          256u
#define VOICE_AUDIO_ID_MAX            33u
#define VOICE_AUDIO_MAX_BYTES         384000u

typedef struct
{
    char turn_id[VOICE_TURN_ID_MAX];
    char transcript[VOICE_TRANSCRIPT_MAX];
    char reply_text[VOICE_REPLY_TEXT_MAX];
    VoiceAction action;
    char audio_id[VOICE_AUDIO_ID_MAX];
    uint32_t expires_in;
} VoiceResponse;

int voice_response_parse(const char *json, size_t size, VoiceResponse *out);
int voice_response_audio_size_valid(size_t size);

#endif /* VOICE_RESPONSE_H */
