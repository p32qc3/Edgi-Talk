#ifndef PET_STORE_H
#define PET_STORE_H
#include "pet_logic/edgi/pet_core.h"
typedef struct {
    u32 generation, last_saved_at, writes, failures, damaged, request_id, completed_id;
    u16 saved_trust;
    u8 ready, loaded, pending, saving;
    int error;
} PetStoreStatus;
/* Startup returns 1 if data were restored, 0 if blank, -1 on an unavailable
 * or damaged store. Failed stores are never silently erased. */
int pet_store_init(PetSaved *restored);
int pet_store_request(const PetSaved *saved);
void pet_store_get(PetStoreStatus *out);
/* For the owner to avoid delaying pending interaction updates during a retry. */
int pet_store_busy(void);
int pet_store_growth_pending(const PetSaved *saved);
#endif
