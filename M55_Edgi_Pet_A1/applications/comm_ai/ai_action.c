#include <string.h>

#include "ai_action.h"

int ai_action_decide(const AiEvent *event, AiActionDecision *decision)
{
    unsigned i;

    if (!decision)
        return 0;
    memset(decision, 0, sizeof(*decision));
    if (!event || event->type != AI_EVENT_COMPLETED ||
        event->action > AI_ACTION_START_GAME)
        return 0;

    for (i = 0; i < AI_REPLY_MAX - 1 && event->text[i]; i++)
        decision->text[i] = event->text[i];
    decision->text[i] = '\0';
    if (event->action == AI_ACTION_CELEBRATE)
        decision->celebrate = 1;
    else if (event->action == AI_ACTION_START_GAME)
        decision->start_simon = 1;
    return 1;
}

void ai_voice_action_bridge_init(AiVoiceActionBridge *bridge)
{
    if (bridge) memset(bridge, 0, sizeof(*bridge));
}

int ai_voice_action_bridge_decide(AiVoiceActionBridge *bridge,
                                  u32 turn_id,
                                  VoiceAction action,
                                  u8 simon_online,
                                  AiVoiceActionDecision *decision)
{
    if (decision) decision->dispatch = AI_VOICE_DISPATCH_NONE;
    if (!bridge || !decision || !turn_id || turn_id == bridge->last_turn_id)
        return 0;

    switch (action) {
    case VOICE_ACTION_SHOW_STATUS:
        decision->dispatch = AI_VOICE_DISPATCH_SHOW_STATUS;
        break;
    case VOICE_ACTION_HOME:
        decision->dispatch = AI_VOICE_DISPATCH_HOME;
        break;
    case VOICE_ACTION_START_SIMON:
        decision->dispatch = simon_online ? AI_VOICE_DISPATCH_START_SIMON :
                                            AI_VOICE_DISPATCH_SIMON_OFFLINE;
        break;
    default:
        return 0;
    }
    bridge->last_turn_id = turn_id;
    return 1;
}
