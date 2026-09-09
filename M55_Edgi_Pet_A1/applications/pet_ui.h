#ifndef PET_UI_H
#define PET_UI_H
#include "pet_logic/edgi/pet_core.h"
#include <lvgl.h>

enum PetFeedback { PET_FEEDBACK_NONE, PET_FEEDBACK_HEAD,
                   PET_FEEDBACK_COOLDOWN, PET_FEEDBACK_HUG, PET_FEEDBACK_LIGHT };
enum PetTestTouch { PET_TEST_TOUCH_UP, PET_TEST_TOUCH_DOWN, PET_TEST_TOUCH_MOVE };
void pet_ui_create(lv_obj_t *parent);
void pet_ui_update(const PetSnapshot *snapshot, u8 feedback, u32 now);
void pet_ui_reset_input(void);
void pet_ui_set_active(u8 active);
/* Owner-thread-only diagnostic pointer; it passes through LVGL hit testing. */
int pet_ui_test_touch(u8 action, u16 x, u16 y);
void pet_ui_log_layout(void);

#endif
