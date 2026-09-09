#include "storage.h"
#include "../common/protocol.h"
#include <string.h>
static int valid(const PetSaved *s) {
    return s->trust_x100<=PET_TRUST_MAX &&
        s->quota_used<=PET_QUOTA_MAX &&
        s->window_ms<PET_WINDOW_MS && s->wins<=s->games;
}
int save_encode(const PetSaved *s, u32 generation, u8 out[SAVE_SIZE]) {
    if (!valid(s)) return 0;
    memset(out,0,SAVE_SIZE);
    memcpy(out,"PET1",4); out[4]=1; out[5]=SAVE_SIZE;
    put_u32(out+6,generation);
    put_u16(out+10,s->trust_x100);
    put_u16(out+12,s->quota_used);
    memcpy(out+14,s->reward_count,5);
    put_u32(out+19,s->window_ms); put_u32(out+23,s->exp);
    put_u32(out+27,s->interactions); put_u32(out+31,s->games);
    put_u32(out+35,s->wins); put_u32(out+39,s->dizzy_count);
    put_u32(out+43,s->best_reaction_ms);
    /* 47..48 reserved zero for version 1. */
    put_u16(out+49,record_crc16(out,49)); return 1;
}
int save_decode(const u8 in[SAVE_SIZE], PetSaved *s, u32 *generation) {
    PetSaved t;
    if (memcmp(in,"PET1",4) || in[4]!=1 || in[5]!=SAVE_SIZE ||
        in[47] || in[48] || get_u16(in+49)!=record_crc16(in,49))
        return 0;
    memset(&t,0,sizeof(t));
    t.trust_x100=get_u16(in+10); t.quota_used=get_u16(in+12);
    memcpy(t.reward_count,in+14,5);
    t.window_ms=get_u32(in+19); t.exp=get_u32(in+23);
    t.interactions=get_u32(in+27); t.games=get_u32(in+31);
    t.wins=get_u32(in+35); t.dizzy_count=get_u32(in+39);
    t.best_reaction_ms=get_u32(in+43);
    if (!valid(&t)) return 0;
    *s=t; *generation=get_u32(in+6); return 1;
}
int save_choose(const u8 *a, const u8 *b, PetSaved *s, u32 *generation) {
    PetSaved sa, sb;
    u32 ga=0, gb=0, delta;
    int va=save_decode(a,&sa,&ga), vb=save_decode(b,&sb,&gb);
    if (!va && !vb) return -1;
    delta=(u32)(gb-ga);
    if (vb && (!va || (delta && delta<0x80000000UL))) {
        *s=sb; *generation=gb; return 1;
    }
    *s=sa; *generation=ga; return 0;
}
