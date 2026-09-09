#ifndef INTERACTION_HOLD_H
#define INTERACTION_HOLD_H
#include "../common/pet_types.h"

#define PET_HUG_HOLD_MS 2500UL
typedef struct {
    u32 started_at;
    u8 active, fired;
} PetHold;

/* Begin/end correspond to one physical press. Update returns one action only. */
void pet_hold_begin(PetHold *hold, u32 now);
int pet_hold_update(PetHold *hold, u32 now);
void pet_hold_cancel(PetHold *hold);
u8 pet_hold_progress(const PetHold *hold, u32 now);
#endif
