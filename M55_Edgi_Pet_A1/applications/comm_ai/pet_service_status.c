#include <string.h>
#include "pet_service_status.h"
#include "ai_client.h"

/* 全局状态 */
static u8 g_ai_state = PET_AI_DEMO;
static u8 g_51_state = PET_51_WAIT;
static char g_version[8] = "AB1";

void pet_service_status_init(void)
{
    g_ai_state = PET_AI_DEMO;
    g_51_state = PET_51_WAIT;
}

void pet_service_status_set_version(const char *version)
{
    if (version) {
        strncpy(g_version, version, sizeof(g_version) - 1);
        g_version[sizeof(g_version) - 1] = '\0';
    }
}

void pet_service_status_set_ai_state(u8 state)
{
    g_ai_state = state;
}

void pet_service_status_set_51_state(u8 state)
{
    g_51_state = state;
}

u8 pet_service_ai_runtime(u8 service_running, u8 busy,
    u8 has_event, u8 event_type)
{
    if (!service_running)
        return PET_AI_RUNTIME_OFF;
    if (busy)
        return PET_AI_RUNTIME_WAITING;
    if (!has_event)
        return PET_AI_RUNTIME_READY;
    if (event_type==AI_EVENT_COMPLETED)
        return PET_AI_RUNTIME_COMPLETED;
    return PET_AI_RUNTIME_ERROR;
}

int pet_service_status_format(char *out, u16 capacity,
    u8 ai_status, u8 link_ready)
{
    const char *ai_text;
    const char *link_text;
    const char *middle=" | 51:";
    const char *version_prefix=" | ";
    unsigned total;
    char *at;

    if (!out)
        return 0;
    switch (ai_status)
    {
    case PET_AI_RUNTIME_OFF: ai_text="AI:OFF"; break;
    case PET_AI_RUNTIME_READY: ai_text="AI:DEMO"; break;
    case PET_AI_RUNTIME_WAITING: ai_text="AI:THINK"; break;
    case PET_AI_RUNTIME_COMPLETED: ai_text="AI:DONE"; break;
    case PET_AI_RUNTIME_ERROR: ai_text="AI:ERR"; break;
    default: return 0;
    }
    link_text=link_ready ? "LINK" : "WAIT";
    total=(unsigned)strlen(ai_text)+(unsigned)strlen(middle)+
        (unsigned)strlen(link_text)+(unsigned)strlen(version_prefix)+
        (unsigned)strlen(g_version);
    if (capacity<=total)
        return 0;

    at=out;
    memcpy(at,ai_text,strlen(ai_text)); at+=strlen(ai_text);
    memcpy(at,middle,strlen(middle)); at+=strlen(middle);
    memcpy(at,link_text,strlen(link_text)); at+=strlen(link_text);
    memcpy(at,version_prefix,strlen(version_prefix)); at+=strlen(version_prefix);
    memcpy(at,g_version,strlen(g_version)); at+=strlen(g_version);
    *at='\0';
    return 1;
}

int pet_service_status_format_current(char *out, u16 capacity)
{
    u8 runtime;

    if (g_ai_state == PET_AI_BUSY)
        runtime = PET_AI_RUNTIME_WAITING;
    else if (g_ai_state == PET_AI_ERROR)
        runtime = PET_AI_RUNTIME_ERROR;
    else
        runtime = PET_AI_RUNTIME_READY;
    return pet_service_status_format(out, capacity, runtime,
        g_51_state == PET_51_LINK);
}
