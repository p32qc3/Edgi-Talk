#ifndef PROTOCOL_H
#define PROTOCOL_H
#include "pet_types.h"
#define PROTO_VERSION 1
#define PROTO_PAYLOAD_MAX 16
#define PROTO_RAW_MAX 26
#define PROTO_WIRE_MAX 54
enum ProtoCommand {
    CMD_HELLO=1, CMD_READY, CMD_ACK, CMD_HEARTBEAT,
    CMD_STATE=16, CMD_GAME_START, CMD_GAME_CANCEL,
    CMD_BUTTON=32, CMD_GAME_RESULT
};
typedef struct {
    u8 command, length;
    u16 sequence;
    u32 session;
    u8 payload[PROTO_PAYLOAD_MAX];
} ProtoFrame;
typedef struct {
    u8 raw[PROTO_RAW_MAX], size, escaped, collecting;
    u32 last_byte_ms;
} ProtoParser;
u16 get_u16(const u8 *p);
u32 get_u32(const u8 *p);
void put_u16(u8 *p, u16 n);
void put_u32(u8 *p, u32 n);
u8 proto_crc8(const u8 *p, u8 size);
u16 record_crc16(const u8 *p, u16 size);
void proto_init(ProtoParser *p);
/* Returns encoded size, or zero if invalid/capacity insufficient. */
u8 proto_encode(const ProtoFrame *f, u8 *wire, u8 capacity);
/* Feed one byte. 1=frame; 0=incomplete; -1=malformed/reset. */
int proto_feed(ProtoParser *p, u8 byte, u32 now, ProtoFrame *out);
#endif
