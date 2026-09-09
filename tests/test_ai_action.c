#include <stdio.h>
#include <string.h>

#include "ai_action.h"

static int test_completed_simon_starts_game(void)
{
    AiEvent event;
    AiActionDecision decision;

    memset(&event, 0, sizeof(event));
    event.type = AI_EVENT_COMPLETED;
    event.action = AI_ACTION_START_GAME;
    strcpy(event.text, "开始记忆灯游戏");

    return ai_action_decide(&event, &decision) &&
           decision.start_simon == 1 &&
           decision.celebrate == 0 &&
           strcmp(decision.text, "开始记忆灯游戏") == 0;
}

static int test_non_completed_event_is_inert(void)
{
    AiEvent event;
    AiActionDecision decision;

    memset(&event, 0, sizeof(event));
    memset(&decision, 0xA5, sizeof(decision));
    event.type = AI_EVENT_TIMED_OUT;
    event.action = AI_ACTION_START_GAME;

    return !ai_action_decide(&event, &decision) &&
           decision.start_simon == 0 && decision.celebrate == 0 &&
           decision.text[0] == '\0';
}

static int test_unknown_action_is_inert(void)
{
    AiEvent event;
    AiActionDecision decision;

    memset(&event, 0, sizeof(event));
    event.type = AI_EVENT_COMPLETED;
    event.action = 99;

    return !ai_action_decide(&event, &decision) &&
           decision.start_simon == 0 && decision.celebrate == 0;
}

int main(void)
{
    if (!test_completed_simon_starts_game()) {
        fprintf(stderr, "FAIL: completed Simon action must start one game\n");
        return 1;
    }
    if (!test_non_completed_event_is_inert()) {
        fprintf(stderr, "FAIL: timeout/cancel/reject events must be inert\n");
        return 1;
    }
    if (!test_unknown_action_is_inert()) {
        fprintf(stderr, "FAIL: unknown AI actions must be inert\n");
        return 1;
    }
    puts("PASS: ai_action");
    return 0;
}
