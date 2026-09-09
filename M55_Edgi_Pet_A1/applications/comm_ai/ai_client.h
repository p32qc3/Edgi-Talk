#ifndef AI_CLIENT_H
#define AI_CLIENT_H

#include "../common/pet_types.h"

#define AI_REQUEST_TIMEOUT_MS 10000UL
#define AI_REPLY_MAX 96

enum AiAction {
    AI_ACTION_TEXT=0,
    AI_ACTION_CELEBRATE=1,
    AI_ACTION_START_GAME=2
};

enum AiEventType {
    AI_EVENT_NONE=0,
    AI_EVENT_COMPLETED,
    AI_EVENT_TIMED_OUT,
    AI_EVENT_CANCELLED,
    AI_EVENT_REJECTED
};

typedef struct {
    u8 type;
    u8 action;
    u32 request_id;
    char text[AI_REPLY_MAX];
} AiEvent;

typedef struct {
    u32 active_id;
    u32 last_id;
    u32 started_at_ms;
    u8 active;
    u8 event_pending;
    AiEvent event;
} AiClient;

void ai_client_init(AiClient *client);
int ai_client_busy(const AiClient *client);
int ai_client_start(AiClient *client, u32 request_id, u32 now_ms);
void ai_client_tick(AiClient *client, u32 now_ms);
int ai_client_complete(AiClient *client, u32 request_id,
    const char *reply, u8 action, u32 now_ms);
int ai_client_cancel(AiClient *client, u32 request_id);
int ai_client_take_event(AiClient *client, AiEvent *event);

#endif
