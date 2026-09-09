#include <string.h>
#include "ai_client.h"

static void ai_set_event(AiClient *client, u8 type, u32 request_id,
    u8 action, const char *text)
{
    unsigned i;

    client->event.type=type;
    client->event.request_id=request_id;
    client->event.action=action;
    client->event.text[0]='\0';
    if (text)
    {
        for (i=0;i<AI_REPLY_MAX-1 && text[i];i++)
            client->event.text[i]=text[i];
        client->event.text[i]='\0';
    }
    client->event_pending=1;
}

void ai_client_init(AiClient *client)
{
    if (client)
        memset(client,0,sizeof(*client));
}

int ai_client_busy(const AiClient *client)
{
    return client && client->active ? 1 : 0;
}

int ai_client_start(AiClient *client, u32 request_id, u32 now_ms)
{
    if (!client || !request_id || client->active || client->event_pending ||
        request_id<=client->last_id)
        return 0;
    client->active_id=request_id;
    client->last_id=request_id;
    client->started_at_ms=now_ms;
    client->active=1;
    return 1;
}

void ai_client_tick(AiClient *client, u32 now_ms)
{
    u32 request_id;

    if (!client || !client->active)
        return;
    if ((u32)(now_ms-client->started_at_ms)<AI_REQUEST_TIMEOUT_MS)
        return;
    request_id=client->active_id;
    client->active=0;
    client->active_id=0;
    ai_set_event(client,AI_EVENT_TIMED_OUT,request_id,AI_ACTION_TEXT,0);
}

int ai_client_complete(AiClient *client, u32 request_id,
    const char *reply, u8 action, u32 now_ms)
{
    if (!client)
        return 0;
    ai_client_tick(client,now_ms);
    if (!client->active || request_id!=client->active_id)
        return 0;
    client->active=0;
    client->active_id=0;
    if (!reply || action>AI_ACTION_START_GAME)
    {
        ai_set_event(client,AI_EVENT_REJECTED,request_id,
            AI_ACTION_TEXT,0);
        return 1;
    }
    ai_set_event(client,AI_EVENT_COMPLETED,request_id,action,reply);
    return 1;
}

int ai_client_cancel(AiClient *client, u32 request_id)
{
    if (!client || !client->active || request_id!=client->active_id)
        return 0;
    client->active=0;
    client->active_id=0;
    ai_set_event(client,AI_EVENT_CANCELLED,request_id,AI_ACTION_TEXT,0);
    return 1;
}

int ai_client_take_event(AiClient *client, AiEvent *event)
{
    if (!client || !event || !client->event_pending)
        return 0;
    *event=client->event;
    client->event_pending=0;
    return 1;
}
