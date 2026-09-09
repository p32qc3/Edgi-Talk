#ifndef PET_CORE_H
#define PET_CORE_H
#include "../common/pet_types.h"
typedef struct {
    u16 trust_x100, quota_used;
    u8 reward_count[PET_CATEGORIES];
    u32 window_ms, exp, interactions, games, wins, dizzy_count;
    u32 best_reaction_ms;
} PetSaved;
typedef struct {
    PetSaved saved;
    u8 mood, energy, need, base, reaction, sleeping;
    u8 heavy_latched, stable, imu_fault;
    u32 revision, last_tick, positive_at, light_at, spam_at;
    u32 reward_at[PET_CATEGORIES], reaction_at, reaction_ms;
    u32 stable_at, last_imu_at;
    u32 active_game, last_game, game_at, ai_id, last_ai, ai_at;
} PetCore;
typedef struct {
    u8 mood, energy, need, base, display;
    u16 trust_x100;
    u32 exp, revision, active_game;
} PetSnapshot;
void pet_init(PetCore *p, const PetSaved *saved, u32 now);
void pet_tick(PetCore *p, u32 now);
/* Inputs are recognized gestures, not raw touch/IMU samples. */
int pet_input(PetCore *p, u8 input, u32 now);
void pet_imu_stable(PetCore *p, u8 stable, u32 now);
void pet_snapshot(const PetCore *p, PetSnapshot *out);
int pet_begin_game(PetCore *p, u32 id, u32 now);
int pet_finish_game(PetCore *p, u32 id, u8 result,
                    u8 had_input, u32 reaction_ms, u32 now);
int pet_begin_ai(PetCore *p, u32 request_id, u32 now);
/* action: 0=text only; 1=celebrate; 2=request game. */
int pet_finish_ai(PetCore *p, u32 request_id, u8 action, u32 now);
#endif
