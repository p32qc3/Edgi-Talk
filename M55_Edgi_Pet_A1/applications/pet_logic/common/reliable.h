#ifndef RELIABLE_H
#define RELIABLE_H
#include "protocol.h"
typedef struct {
    ProtoFrame pending;
    u32 session, sent_at;
    u16 next_sequence;
    u8 busy, attempts, status;
} ReliableTx;
typedef struct { ProtoFrame last; u8 valid, status; u32 session; } ReliableRx;
void tx_init(ReliableTx *t, u32 session);
int tx_begin(ReliableTx *t, u8 command, const u8 *p, u8 size);
/* Call only when UART has capacity. 1=send; 0=wait; -1=exhausted. */
int tx_poll(ReliableTx *t, u32 now, ProtoFrame *out);
int tx_ack(ReliableTx *t, const ProtoFrame *ack);
void rx_init(ReliableRx *r, u32 session);
/* 1=new; 2=identical retry; 0=wrong session/stale/conflicting frame. */
int rx_check(const ReliableRx *r, const ProtoFrame *f);
/* Commit only AFTER local admission; cache both success and refusal. */
void rx_commit(ReliableRx *r, const ProtoFrame *f, u8 status);
void make_ack(const ProtoFrame *f, u8 status, ProtoFrame *ack);
#endif
