#ifndef AI_OFFLINE_H
#define AI_OFFLINE_H

#include "ai_response.h"

/* Produces strict allow-listed JSON. Returns byte count, or zero on rejection. */
int ai_offline_respond(const char *command, char *json, u16 capacity);

#endif
