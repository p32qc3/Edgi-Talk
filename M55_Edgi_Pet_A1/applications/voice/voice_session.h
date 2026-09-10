#ifndef VOICE_SESSION_H
#define VOICE_SESSION_H

#include <stdint.h>

#define VOICE_SPEECH_ENERGY       500u
#define VOICE_SILENCE_STOP_MS     1500u
#define VOICE_RECORD_MAX_MS       8000u
#define VOICE_CLOUD_TIMEOUT_MS    25000u

typedef enum
{
    VOICE_ACTION_NONE = 0,
    VOICE_ACTION_SHOW_STATUS,
    VOICE_ACTION_START_SIMON,
    VOICE_ACTION_HOME
} VoiceAction;

typedef enum
{
    VOICE_STATE_IDLE = 0,
    VOICE_STATE_LISTENING,
    VOICE_STATE_THINKING,
    VOICE_STATE_SPEAKING,
    VOICE_STATE_ERROR
} VoiceState;

typedef enum
{
    VOICE_EVENT_TAP = 0,
    VOICE_EVENT_AUDIO_FRAME,
    VOICE_EVENT_RECORD_STOPPED,
    VOICE_EVENT_UPLOAD_OK,
    VOICE_EVENT_UPLOAD_FAILED,
    VOICE_EVENT_PLAY_DONE,
    VOICE_EVENT_TICK,
    VOICE_EVENT_WIFI_LOST,
    VOICE_EVENT_CANCEL
} VoiceEventType;

typedef enum
{
    VOICE_EFFECT_NONE = 0,
    VOICE_EFFECT_RECORD_START,
    VOICE_EFFECT_RECORD_STOP,
    VOICE_EFFECT_UPLOAD,
    VOICE_EFFECT_PLAY_START,
    VOICE_EFFECT_CANCEL,
    VOICE_EFFECT_EXECUTE_ACTION
} VoiceEffectType;

typedef struct
{
    VoiceEventType type;
    uint32_t now_ms;
    uint32_t turn_id;
    uint32_t energy;
    uint8_t has_audio;
    VoiceAction action;
} VoiceEvent;

typedef struct
{
    VoiceEffectType type;
    uint32_t turn_id;
    VoiceAction action;
} VoiceEffect;

typedef struct
{
    VoiceState state;
    uint32_t next_turn_id;
    uint32_t active_turn_id;
    uint32_t phase_started_ms;
    uint32_t last_speech_ms;
    uint8_t speech_seen;
    VoiceAction pending_action;
} VoiceSession;

void voice_session_init(VoiceSession *session);
VoiceEffect voice_session_dispatch(VoiceSession *session,
                                   const VoiceEvent *event);

#endif /* VOICE_SESSION_H */
