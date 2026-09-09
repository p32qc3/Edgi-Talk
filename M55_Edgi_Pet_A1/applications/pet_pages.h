#ifndef PET_PAGES_H
#define PET_PAGES_H
#include "pet_app.h"
/* LVGL owner only. All page objects are created once and share one PetCore. */
void pet_pages_create(void);
void pet_pages_show(u8 page);
void pet_pages_reset(void);
u8 pet_pages_current(void);
void pet_pages_update(const PetAppView *view, u32 now);
void pet_pages_log_layout(void);
#endif
