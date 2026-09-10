#ifndef VOICE_SERVICE_H
#define VOICE_SERVICE_H

#include <stdint.h>

#include "voice_session.h"
#include "voice_response.h"

typedef enum
{
    VOICE_VIEW_UNCONFIGURED = 0,
    VOICE_VIEW_CONNECTING,
    VOICE_VIEW_READY,
    VOICE_VIEW_LISTENING,
    VOICE_VIEW_THINKING,
    VOICE_VIEW_SPEAKING,
    VOICE_VIEW_ERROR
} VoiceViewState;

typedef struct
{
    VoiceViewState state;
    uint8_t configured;
    uint8_t wifi_online;
    uint32_t turn_id;
    int error_code;
    char transcript[VOICE_TRANSCRIPT_MAX];
    char reply_text[VOICE_REPLY_TEXT_MAX];
} VoiceView;

void voice_service_toggle(void);
void voice_service_cancel(void);
void voice_service_get_view(VoiceView *view);
int voice_service_take_action(uint32_t *turn_id, VoiceAction *action);

#endif /* VOICE_SERVICE_H */
