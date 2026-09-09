#include "pet_core.h"
#include "pet_balance.h"
#include <string.h>
static u8 clamp100(int n) {
    return (u8)(n<0 ? 0 : (n>100 ? 100 : n));
}
static u8 priority(u8 e) {
    if (e==PET_DIZZY) return 90;
    if (e==PET_ANGRY) return 70;
    if (e==PET_EXCITED) return 60;
    if (e==PET_HAPPY) return 50;
    return 0;
}
static void base_update(PetCore *p) {
    if (p->base==PET_SAD && p->mood<40) return;
    if (p->base==PET_HAPPY && p->mood>60) return;
    p->base=p->mood<=30 ? PET_SAD :
            (p->mood>=70 ? PET_HAPPY : PET_NORMAL);
}
static void react(PetCore *p, u8 e, u32 ms, u32 now) {
    if (!p->reaction_ms || priority(e)>=priority(p->reaction)) {
        p->reaction=e; p->reaction_ms=ms; p->reaction_at=now;
    }
}
static void reward(PetCore *p, u8 category, u16 value, u32 now) {
    u32 cooldown=category==0 ? PET_POSITIVE_COOLDOWN_MS : 60000UL;
    u16 amount;
    u8 count;
    unsigned full=category==0 ? PET_POSITIVE_FULL_REWARDS : 5U;
    unsigned half=category==0 ? PET_POSITIVE_HALF_REWARDS : 5U;
    if ((u32)(now-p->reward_at[category])<cooldown) return;
    p->reward_at[category]=now;
    count=p->saved.reward_count[category];
    if (count<255) p->saved.reward_count[category]++;
    /* Round a half reward up to the smallest stored unit (0.01 trust),
     * so a nonzero reward cannot disappear through integer truncation. */
    amount=count<full ? value : (count<full+half ? value/2+value%2 : 0);
    if (amount>PET_QUOTA_MAX-p->saved.quota_used)
        amount=(u16)(PET_QUOTA_MAX-p->saved.quota_used);
    if (amount>PET_TRUST_MAX-p->saved.trust_x100)
        amount=(u16)(PET_TRUST_MAX-p->saved.trust_x100);
    p->saved.trust_x100+=amount;
    p->saved.quota_used+=amount;
}
void pet_init(PetCore *p, const PetSaved *saved, u32 now) {
    u8 i;
    memset(p,0,sizeof(*p));
    if (saved) p->saved=*saved;
    p->mood=PET_INITIAL_MOOD; p->energy=80; p->need=30;
    p->last_tick=now; p->positive_at=now-PET_POSITIVE_COOLDOWN_MS;
    p->light_at=now-5000UL; p->spam_at=now-5000UL;
    for (i=0;i<PET_CATEGORIES;i++) p->reward_at[i]=now;
    /* A new pet rewards its first touch immediately. Restoring saved data
     * keeps the category cooldown as well as saved counts and quota. */
    if (!saved) p->reward_at[0]=now-PET_POSITIVE_COOLDOWN_MS;
    p->revision=1; base_update(p);
}
void pet_tick(PetCore *p, u32 now) {
    u32 dt=(u32)(now-p->last_tick), left;
    p->last_tick=now;
    left=PET_WINDOW_MS-p->saved.window_ms;
    if (dt>=left) {
        p->saved.window_ms=(dt-left)%PET_WINDOW_MS;
        p->saved.quota_used=0;
        memset(p->saved.reward_count,0,sizeof(p->saved.reward_count));
    } else p->saved.window_ms+=dt;
    if (p->reaction_ms) {
        dt=(u32)(now-p->reaction_at);
        if (p->reaction==PET_DIZZY) {
            if (dt>=10000UL ||
                (dt>=3000UL && p->stable &&
                 (u32)(now-p->stable_at)>=1000UL &&
                 (u32)(now-p->last_imu_at)<=200UL)) {
                p->imu_fault=(u8)(dt>=10000UL);
                p->reaction_ms=0; p->heavy_latched=0;
                p->revision++;
            }
        } else if (dt>=p->reaction_ms) {
            p->reaction_ms=0; p->revision++;
        }
    }
    if (p->ai_id && (u32)(now-p->ai_at)>=10000UL) {
        p->ai_id=0; p->revision++;
    }
    base_update(p);
}
void pet_imu_stable(PetCore *p, u8 stable, u32 now) {
    if ((u32)(now-p->last_imu_at)>200UL) p->stable=0;
    p->last_imu_at=now;
    if (stable && !p->stable) p->stable_at=now;
    p->stable=stable;
    pet_tick(p,now);
}
int pet_input(PetCore *p, u8 input, u32 now) {
    pet_tick(p,now);
    if (input==INPUT_SLEEP) {
        p->sleeping=1; p->ai_id=0; p->active_game=0;
        p->reaction_ms=0; p->heavy_latched=0; p->revision++;
        return 1;
    }
    if (input==INPUT_WAKE) {
        p->sleeping=0; p->revision++; return 1;
    }
    if (p->sleeping) return 0;
    if (input==INPUT_HEAD || input==INPUT_HUG) {
        if ((u32)(now-p->positive_at)<PET_POSITIVE_COOLDOWN_MS) return 0;
        p->positive_at=now;
        p->mood=clamp100(p->mood+(input==INPUT_HEAD ? PET_HEAD_MOOD_GAIN : PET_HUG_MOOD_GAIN));
        p->need=clamp100(p->need-(input==INPUT_HEAD ? 5 : 8));
        reward(p,0,(u16)(input==INPUT_HEAD ? PET_HEAD_TRUST_GAIN : PET_HUG_TRUST_GAIN),now);
        /* A brief response is independent of the long-term mood threshold. */
        react(p,PET_HAPPY,input==INPUT_HEAD ? 1000UL : PET_HUG_REACTION_MS,now);
    } else if (input==INPUT_LIGHT) {
        if (p->heavy_latched) return 0;
        if ((u32)(now-p->light_at)<5000UL) return 0;
        p->light_at=now; p->mood=clamp100(p->mood+3);
        p->energy=clamp100(p->energy-1); reward(p,1,10,now);
        react(p,PET_HAPPY,1200UL,now);
    } else if (input==INPUT_SPAM) {
        if ((u32)(now-p->spam_at)<5000UL) return 0;
        p->spam_at=now; p->mood=clamp100(p->mood-8);
        react(p,PET_ANGRY,2000,now);
    } else if (input==INPUT_HEAVY) {
        if (p->heavy_latched) return 0;
        p->heavy_latched=1; p->stable=0; p->imu_fault=0;
        p->mood=clamp100(p->mood-15);
        p->energy=clamp100(p->energy-5);
        p->saved.dizzy_count++; p->active_game=0; p->ai_id=0;
        react(p,PET_DIZZY,10000,now);
    } else return 0;
    p->saved.interactions++; p->revision++; base_update(p);
    return 1;
}
void pet_snapshot(const PetCore *p, PetSnapshot *out) {
    out->mood=p->mood; out->energy=p->energy; out->need=p->need;
    out->base=p->base; out->trust_x100=p->saved.trust_x100;
    out->exp=p->saved.exp; out->revision=p->revision;
    out->active_game=p->active_game;
    out->display=p->sleeping ? PET_SLEEP :
        (p->reaction_ms ? p->reaction :
         (p->ai_id ? PET_THINKING : p->base));
}
int pet_begin_game(PetCore *p, u32 id, u32 now) {
    pet_tick(p,now);
    if (!id || id<=p->last_game || p->active_game ||
        p->sleeping || p->heavy_latched || p->ai_id) return 0;
    p->active_game=id; p->last_game=id;
    p->game_at=now; p->revision++; return 1;
}
int pet_finish_game(PetCore *p, u32 id, u8 result,
                    u8 had_input, u32 reaction_ms, u32 now) {
    u32 elapsed;
    pet_tick(p,now);
    if (!id || id!=p->active_game || result<1 || result>4 ||
        had_input>1 || (result==GAME_WIN && !had_input)) return 0;
    elapsed=(u32)(now-p->game_at);
    p->active_game=0; p->revision++;
    if (!had_input || result>=GAME_CANCEL) return 1;
    p->saved.games++;
    if (result==GAME_WIN) {
        p->saved.wins++; p->saved.exp+=10;
        p->mood=clamp100(p->mood+5);
        react(p,PET_EXCITED,1500,now);
    }
    if (elapsed>=10000UL)
        reward(p,2,(u16)(result==GAME_WIN ? 50 : 20),now);
    if (result==GAME_WIN && reaction_ms &&
        (!p->saved.best_reaction_ms ||
         reaction_ms<p->saved.best_reaction_ms))
        p->saved.best_reaction_ms=reaction_ms;
    base_update(p); return 1;
}
int pet_begin_ai(PetCore *p, u32 id, u32 now) {
    pet_tick(p,now);
    if (!id || id<=p->last_ai || p->ai_id || p->sleeping || p->active_game ||
        p->heavy_latched) return 0;
    p->ai_id=id; p->last_ai=id; p->ai_at=now; p->revision++; return 1;
}
int pet_finish_ai(PetCore *p, u32 id, u8 action, u32 now) {
    pet_tick(p,now);
    if (!id || id!=p->ai_id) return 0;
    p->ai_id=0; p->revision++;
    if (action>2 || p->sleeping || p->heavy_latched ||
        p->active_game) return 0;
    if (action==1) react(p,PET_EXCITED,1500,now);
    /* Return 3 asks the owner to allocate a new game id/start handshake. */
    return (int)action+1;
}
