#ifndef MESSAGES_H
#define MESSAGES_H
#include "protocol.h"
/* Call after CRC decode and before application dispatch. */
int message_valid(const ProtoFrame *f);
#endif
