#ifndef AI_RESPONSE_H
#define AI_RESPONSE_H

#include "ai_client.h"

#define AI_RESPONSE_JSON_MAX 256

typedef struct {
    u8 action;
    char text[AI_REPLY_MAX];
} AiParsedResponse;

/* Strict schema: {"text":"...","action":"text|celebrate|simon"}. */
int ai_response_parse(const char *json, u32 size, AiParsedResponse *out);
/* The only supported path from raw network bytes into an AI request. */
int ai_response_complete(AiClient *client, u32 request_id,
    const char *json, u32 size, u32 now_ms);

#endif
