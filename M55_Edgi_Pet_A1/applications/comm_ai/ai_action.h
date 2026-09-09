#ifndef AI_ACTION_H
#define AI_ACTION_H

#include "ai_client.h"

typedef struct {
    u8 start_simon;
    u8 celebrate;
    char text[AI_REPLY_MAX];
} AiActionDecision;

/* Returns one only for a completed, allow-listed AI action. */
int ai_action_decide(const AiEvent *event, AiActionDecision *decision);

#endif
