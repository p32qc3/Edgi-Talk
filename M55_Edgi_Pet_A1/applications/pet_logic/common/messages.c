#include "messages.h"
int message_valid(const ProtoFrame *f) {
    const u8 *p=f->payload;
    u32 id;
    if (!f->session || f->length>16) return 0;
    switch (f->command) {
    case CMD_HELLO: case CMD_READY: case CMD_HEARTBEAT:
        return f->length==0;
    case CMD_ACK:
        return f->length==2 && p[0]!=CMD_ACK && p[1]<=3;
    case CMD_STATE:
        return f->length==8 &&
            (p[4]==PET_NORMAL || p[4]==PET_HAPPY || p[4]==PET_SAD) &&
            p[5]<=PET_EXCITED && get_u16(p+6)>0 && get_u16(p+6)<=2000;
    case CMD_GAME_START:
        return f->length==9 && get_u32(p)>0 && p[4]==1;
    case CMD_GAME_CANCEL:
        return f->length==4 && get_u32(p)>0;
    case CMD_BUTTON:
        return f->length==1 && p[0]<16;
    case CMD_GAME_RESULT:
        if (f->length!=12) return 0;
        id=get_u32(p);
        return id>0 && p[4]>=GAME_WIN && p[4]<=GAME_TIMEOUT &&
            p[5]<=1 && !(p[4]==GAME_WIN && !p[5]);
    default: return 0;
    }
}
