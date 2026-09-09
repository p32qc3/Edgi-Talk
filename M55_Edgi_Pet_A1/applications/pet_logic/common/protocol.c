#include "protocol.h"
#include <string.h>
u16 get_u16(const u8 *p) {
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}
u32 get_u32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1]<<8) |
           ((u32)p[2]<<16) | ((u32)p[3]<<24);
}
void put_u16(u8 *p, u16 n) {
    p[0]=(u8)n; p[1]=(u8)(n>>8);
}
void put_u32(u8 *p, u32 n) {
    p[0]=(u8)n; p[1]=(u8)(n>>8);
    p[2]=(u8)(n>>16); p[3]=(u8)(n>>24);
}
u8 proto_crc8(const u8 *p, u8 size) {
    u8 c=0, i;
    while (size--) {
        c^=*p++;
        for (i=0;i<8;i++)
            c=(u8)((c&0x80) ? (c<<1)^0x07 : c<<1);
    }
    return c;
}
u16 record_crc16(const u8 *p, u16 size) {
    u16 c=0xffff;
    u8 i;
    while (size--) {
        c^=(u16)((u16)*p++<<8);
        for (i=0;i<8;i++)
            c=(u16)((c&0x8000) ? (c<<1)^0x1021 : c<<1);
    }
    return c;
}
void proto_init(ProtoParser *p) { memset(p,0,sizeof(*p)); }
u8 proto_encode(const ProtoFrame *f, u8 *wire, u8 capacity) {
    u8 raw[PROTO_RAW_MAX], n, i, size, needed=2;
    if (f->length>PROTO_PAYLOAD_MAX) return 0;
    raw[0]=PROTO_VERSION; raw[1]=f->command;
    raw[2]=f->length; put_u16(raw+3,f->sequence);
    put_u32(raw+5,f->session);
    memcpy(raw+9,f->payload,f->length);
    size=(u8)(10+f->length);
    raw[size-1]=proto_crc8(raw,(u8)(size-1));
    for (i=0;i<size;i++)
        needed+=(raw[i]==0x7e || raw[i]==0x7d) ? 2 : 1;
    if (capacity<needed) return 0;
    n=0; wire[n++]=0x7e;
    for (i=0;i<size;i++) {
        if (raw[i]==0x7e || raw[i]==0x7d) {
            wire[n++]=0x7d; wire[n++]=(u8)(raw[i]^0x20);
        } else wire[n++]=raw[i];
    }
    wire[n++]=0x7e;
    return n;
}
int proto_feed(ProtoParser *p, u8 byte, u32 now, ProtoFrame *out) {
    u8 n;
    int result=0;
    if (p->collecting && (u32)(now-p->last_byte_ms)>100)
        proto_init(p);
    p->last_byte_ms=now;
    if (byte==0x7e) {
        n=p->size;
        if (p->collecting && n) {
            result=-1;
            if (!p->escaped && n>=10 && p->raw[0]==1 &&
                p->raw[2]<=16 && n==(u8)(10+p->raw[2]) &&
                proto_crc8(p->raw,(u8)(n-1))==p->raw[n-1]) {
                out->command=p->raw[1]; out->length=p->raw[2];
                out->sequence=get_u16(p->raw+3);
                out->session=get_u32(p->raw+5);
                memcpy(out->payload,p->raw+9,out->length);
                result=1;
            }
        }
        p->size=0; p->escaped=0; p->collecting=1;
        return result;
    }
    if (!p->collecting) return 0;
    if (p->escaped) {
        if (byte!=0x5d && byte!=0x5e) {
            proto_init(p); return -1;
        }
        byte^=0x20; p->escaped=0;
    } else if (byte==0x7d) { p->escaped=1; return 0; }
    if (p->size>=PROTO_RAW_MAX) { proto_init(p); return -1; }
    p->raw[p->size++]=byte;
    return 0;
}
