#ifndef AI_ACTION_H
#define AI_ACTION_H

#include "ai_client.h"
#include "../voice/voice_session.h"

typedef struct {
    u8 start_simon;
    u8 celebrate;
    char text[AI_REPLY_MAX];
} AiActionDecision;

/* Returns one only for a completed, allow-listed AI action. */
int ai_action_decide(const AiEvent *event, AiActionDecision *decision);

typedef enum {
    AI_VOICE_DISPATCH_NONE = 0,
    AI_VOICE_DISPATCH_SHOW_STATUS,
    AI_VOICE_DISPATCH_HOME,
    AI_VOICE_DISPATCH_START_SIMON,
    AI_VOICE_DISPATCH_SIMON_OFFLINE
} AiVoiceDispatch;

typedef struct {
    u32 last_turn_id;
} AiVoiceActionBridge;

typedef struct {
    AiVoiceDispatch dispatch;
} AiVoiceActionDecision;

void ai_voice_action_bridge_init(AiVoiceActionBridge *bridge);

/* Accepts only the cloud action allow-list and consumes each turn once. */
int ai_voice_action_bridge_decide(AiVoiceActionBridge *bridge,
                                  u32 turn_id,
                                  VoiceAction action,
                                  u8 simon_online,
                                  AiVoiceActionDecision *decision);

#endif
