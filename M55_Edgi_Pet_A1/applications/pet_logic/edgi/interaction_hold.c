#include "interaction_hold.h"

void pet_hold_begin(PetHold *hold, u32 now) {
    hold->started_at=now;
    hold->active=1;
    hold->fired=0;
}
int pet_hold_update(PetHold *hold, u32 now) {
    if (!hold->active || hold->fired ||
        (u32)(now-hold->started_at)<PET_HUG_HOLD_MS) return 0;
    hold->fired=1;
    return 1;
}
void pet_hold_cancel(PetHold *hold) {
    hold->active=0;
    hold->fired=0;
}
u8 pet_hold_progress(const PetHold *hold, u32 now) {
    u32 elapsed;
    if (!hold->active) return 0;
    elapsed=(u32)(now-hold->started_at);
    if (hold->fired || elapsed>=PET_HUG_HOLD_MS) return 100;
    return (u8)(elapsed*100UL/PET_HUG_HOLD_MS);
}
