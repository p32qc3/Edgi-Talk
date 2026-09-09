#include <stdio.h>
#include <string.h>

#include "ai_offline.h"
#include "ai_response.h"

static int expect_action(const char *input, u8 action)
{
    char json[AI_RESPONSE_JSON_MAX];
    AiParsedResponse parsed;
    int size = ai_offline_respond(input, json, sizeof(json));

    return size > 0 && ai_response_parse(json, (u32)size, &parsed) &&
           parsed.action == action && parsed.text[0] != '\0';
}

int main(void)
{
    char json[AI_RESPONSE_JSON_MAX] = "not-empty";

    if (!expect_action("开始游戏", AI_ACTION_START_GAME) ||
        !expect_action("记忆灯", AI_ACTION_START_GAME) ||
        !expect_action("simon", AI_ACTION_START_GAME)) {
        fprintf(stderr, "FAIL: supported game phrases must produce a Simon action\n");
        return 1;
    }
    if (!expect_action("查看状态", AI_ACTION_TEXT) ||
        !expect_action("status", AI_ACTION_TEXT)) {
        fprintf(stderr, "FAIL: status phrases must produce a text action\n");
        return 1;
    }
    if (ai_offline_respond("删除所有数据", json, sizeof(json)) != 0 || json[0] != '\0') {
        fprintf(stderr, "FAIL: unknown or unsafe phrases must be rejected\n");
        return 1;
    }
    puts("PASS: ai_offline");
    return 0;
}
