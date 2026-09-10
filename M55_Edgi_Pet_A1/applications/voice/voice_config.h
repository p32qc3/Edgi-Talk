#ifndef VOICE_CONFIG_H
#define VOICE_CONFIG_H

#include <string.h>

#if defined(__has_include)
#if __has_include("voice_private_config.h")
#include "voice_private_config.h"
#endif
#endif

#ifndef VOICE_PRIVATE_CONFIG_READY
#define VOICE_PRIVATE_CONFIG_READY 0
#define VOICE_WIFI_SSID             "UNSET"
#define VOICE_WIFI_PASSWORD         "UNSET"
#define VOICE_ECS_HOST              "UNSET"
#define VOICE_DEVICE_ID             "UNSET"
#define VOICE_DEVICE_TOKEN          "UNSET"
#endif

#ifndef VOICE_ECS_PORT
#define VOICE_ECS_PORT 80
#endif

#if defined(NDEBUG) && !VOICE_PRIVATE_CONFIG_READY
#error "Fill voice_private_config.h before a release build"
#endif

#define VOICE_HTTP_CONNECT_TIMEOUT_MS 5000u
#define VOICE_HTTP_TURN_TIMEOUT_MS    25000u
#define VOICE_HTTP_RETRY_COUNT        1u
#define VOICE_RECORD_BUFFER_BYTES     256000u

static inline int voice_config_is_ready(void)
{
    return VOICE_PRIVATE_CONFIG_READY &&
           strcmp(VOICE_WIFI_SSID, "UNSET") != 0 &&
           strcmp(VOICE_ECS_HOST, "UNSET") != 0 &&
           strcmp(VOICE_DEVICE_ID, "UNSET") != 0 &&
           strcmp(VOICE_DEVICE_TOKEN, "UNSET") != 0 &&
           strlen(VOICE_DEVICE_TOKEN) >= 32u;
}

#endif /* VOICE_CONFIG_H */
