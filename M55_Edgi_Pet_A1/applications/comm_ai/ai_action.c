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
