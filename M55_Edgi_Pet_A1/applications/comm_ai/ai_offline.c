#include <string.h>

#include "ai_offline.h"

int ai_offline_respond(const char *command, char *json, u16 capacity)
{
    const char *response;
    u16 size;

    if (!json || !capacity)
        return 0;
    json[0] = '\0';
    if (!command)
        return 0;
    if (!strcmp(command, "开始游戏") || !strcmp(command, "记忆灯") ||
        !strcmp(command, "simon"))
        response = "{\"text\":\"开始记忆灯游戏\",\"action\":\"simon\"}";
    else if (!strcmp(command, "查看状态") || !strcmp(command, "状态") ||
             !strcmp(command, "status"))
        response = "{\"text\":\"宠物状态已显示\",\"action\":\"text\"}";
    else
        return 0;

    size = (u16)strlen(response);
    if (size >= capacity)
        return 0;
    memcpy(json, response, size + 1);
    return size;
}
