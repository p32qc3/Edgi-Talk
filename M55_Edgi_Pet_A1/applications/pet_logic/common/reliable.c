#include "reliable.h"
#include <string.h>
void tx_init(ReliableTx *t, u32 session) {
    memset(t,0,sizeof(*t)); t->session=session; t->next_sequence=1;
}
int tx_begin(ReliableTx *t, u8 command, const u8 *p, u8 size) {
    if (t->busy || !t->session || size>16 || command==CMD_ACK) return 0;
    t->pending.command=command; t->pending.length=size;
    t->pending.sequence=t->next_sequence++;
    t->pending.session=t->session;
    if (size) memcpy(t->pending.payload,p,size);
    t->busy=1; t->attempts=0; t->status=255; return 1;
}
int tx_poll(ReliableTx *t, u32 now, ProtoFrame *out) {
    if (!t->busy) return 0;
    if (t->attempts && (u32)(now-t->sent_at)<500) return 0;
    if (t->attempts==4) { t->busy=0; t->status=254; return -1; }
    *out=t->pending; t->attempts++; t->sent_at=now; return 1;
}
int tx_ack(ReliableTx *t, const ProtoFrame *a) {
    if (!t->busy || !t->attempts || a->command!=CMD_ACK ||
        a->session!=t->session || a->sequence!=t->pending.sequence ||
        a->length!=2 || a->payload[0]!=t->pending.command ||
        a->payload[1]>3) return 0;
    t->busy=0; t->status=a->payload[1]; return 1;
}
void rx_init(ReliableRx *r, u32 session) {
    memset(r,0,sizeof(*r)); r->session=session;
}
int rx_check(const ReliableRx *r, const ProtoFrame *f) {
    u16 delta;
    if (!r->session || f->session!=r->session || f->length>16 ||
        f->command==CMD_ACK) return 0;
    if (!r->valid) return 1;
    delta=(u16)(f->sequence-r->last.sequence);
    if (delta) return delta<0x8000U ? 1 : 0;
    return f->command==r->last.command && f->length==r->last.length &&
        !memcmp(f->payload,r->last.payload,f->length) ? 2 : 0;
}
void rx_commit(ReliableRx *r, const ProtoFrame *f, u8 status) {
    r->last=*f; r->valid=1; r->status=status;
}
void make_ack(const ProtoFrame *f, u8 status, ProtoFrame *ack) {
    memset(ack,0,sizeof(*ack)); ack->command=CMD_ACK;
    ack->session=f->session; ack->sequence=f->sequence;
    ack->length=2; ack->payload[0]=f->command; ack->payload[1]=status;
}
