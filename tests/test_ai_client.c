#include <stdio.h>
#include <string.h>

#include "ai_client.h"
#include "ai_response.h"

static int test_timeout_is_reported_once(void)
{
    AiClient client;
    AiEvent event;

    ai_client_init(&client);
    if (!ai_client_start(&client, 1, 100) || !ai_client_busy(&client))
        return 0;
    ai_client_tick(&client, 10099);
    if (!ai_client_busy(&client) || ai_client_take_event(&client, &event))
        return 0;
    ai_client_tick(&client, 10100);
    return !ai_client_busy(&client) && ai_client_take_event(&client, &event) &&
           event.type == AI_EVENT_TIMED_OUT && event.request_id == 1 &&
           !ai_client_take_event(&client, &event);
}

static int test_strict_json_completes_simon(void)
{
    static const char reply[] =
        "{\"text\":\"开始记忆灯游戏\",\"action\":\"simon\"}";
    AiClient client;
    AiEvent event;

    ai_client_init(&client);
    return ai_client_start(&client, 7, 0) &&
           ai_response_complete(&client, 7, reply, sizeof(reply) - 1, 1) &&
           ai_client_take_event(&client, &event) &&
           event.type == AI_EVENT_COMPLETED &&
           event.action == AI_ACTION_START_GAME &&
           strcmp(event.text, "开始记忆灯游戏") == 0;
}

static int test_unknown_json_action_is_rejected(void)
{
    static const char reply[] =
        "{\"text\":\"危险动作\",\"action\":\"erase\"}";
    AiClient client;
    AiEvent event;

    ai_client_init(&client);
    return ai_client_start(&client, 9, 0) &&
           ai_response_complete(&client, 9, reply, sizeof(reply) - 1, 1) &&
           ai_client_take_event(&client, &event) &&
           event.type == AI_EVENT_REJECTED;
}

int main(void)
{
    if (!test_timeout_is_reported_once()) {
        fprintf(stderr, "FAIL: timeout contract\n");
        return 1;
    }
    if (!test_strict_json_completes_simon()) {
        fprintf(stderr, "FAIL: strict Simon response\n");
        return 1;
    }
    if (!test_unknown_json_action_is_rejected()) {
        fprintf(stderr, "FAIL: unknown AI action rejection\n");
        return 1;
    }
    puts("PASS: ai_client");
    return 0;
}
