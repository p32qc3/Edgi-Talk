#include <stdio.h>
#include <string.h>

#include "ai_action.h"

static int expect_action(AiVoiceActionBridge *bridge, unsigned turn_id,
                         VoiceAction action, int simon_online,
                         AiVoiceDispatch expected)
{
    AiVoiceActionDecision decision;
    memset(&decision, 0xa5, sizeof(decision));
    return ai_voice_action_bridge_decide(bridge, turn_id, action,
                                         (u8)simon_online, &decision) == 1 &&
           decision.dispatch == expected;
}

int main(void)
{
    AiVoiceActionBridge bridge;
    AiVoiceActionDecision decision;

    ai_voice_action_bridge_init(&bridge);
    if (!expect_action(&bridge, 1u, VOICE_ACTION_SHOW_STATUS, 0,
                       AI_VOICE_DISPATCH_SHOW_STATUS) ||
        !expect_action(&bridge, 2u, VOICE_ACTION_HOME, 0,
                       AI_VOICE_DISPATCH_HOME) ||
        !expect_action(&bridge, 3u, VOICE_ACTION_START_SIMON, 1,
                       AI_VOICE_DISPATCH_START_SIMON) ||
        !expect_action(&bridge, 4u, VOICE_ACTION_START_SIMON, 0,
                       AI_VOICE_DISPATCH_SIMON_OFFLINE)) {
        fprintf(stderr, "FAIL: safe voice actions are not mapped correctly\n");
        return 1;
    }

    memset(&decision, 0xa5, sizeof(decision));
    if (ai_voice_action_bridge_decide(&bridge, 4u, VOICE_ACTION_START_SIMON,
                                      0u, &decision) != 0 ||
        decision.dispatch != AI_VOICE_DISPATCH_NONE) {
        fprintf(stderr, "FAIL: duplicate turn was executed twice\n");
        return 1;
    }

    if (ai_voice_action_bridge_decide(&bridge, 5u, VOICE_ACTION_NONE,
                                      1u, &decision) != 0 ||
        ai_voice_action_bridge_decide(&bridge, 6u, (VoiceAction)99,
                                      1u, &decision) != 0) {
        fprintf(stderr, "FAIL: unsafe or empty action was accepted\n");
        return 1;
    }

    puts("PASS: voice_action_bridge");
    return 0;
}
