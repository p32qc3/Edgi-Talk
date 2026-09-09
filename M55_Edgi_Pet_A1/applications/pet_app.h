#ifndef PET_APP_H
#define PET_APP_H

#include <rtthread.h>
#include "pet_logic/edgi/pet_core.h"
#include "pet_game.h"
#include "pet_store.h"

enum PetPage { PET_PAGE_HOME, PET_PAGE_GAMES, PET_PAGE_STATUS };
typedef struct {
    PetSnapshot pet;
    PetGame game;
    u32 interactions, physical_clicks, hugs, physical_hugs;
    u32 games, wins, best_reaction_ms;
    u32 light_shakes, heavy_shakes, physical_light, physical_heavy;
    u8 feedback, page, motion_ready;
    PetStoreStatus store;
    u8 diagnostic, growth_pending;
} PetAppView;

/* All PetCore writes and all LVGL operations belong to the LVGL thread.
 * Other M55 threads may only post events or copy the published snapshot. */
int pet_app_init(void);
int pet_app_post_head(rt_bool_t from_touch);
int pet_app_post_hug(rt_bool_t from_touch);
/* Recognized motion from the M55 sensor worker, not raw samples. */
int pet_app_post_motion(u8 motion, u32 sampled_at, rt_bool_t physical);
int pet_app_get_snapshot(PetSnapshot *out);
int pet_app_get_view(PetAppView *out);
int pet_app_navigate(u8 page);
int pet_app_request_game(u8 type);
int pet_app_cancel_game(void);
int pet_app_save(void);

/* B's M55 task uses these nonblocking queues after a successful handshake.
 * session=0 means disconnected; capabilities bit 0 means Simon is available.
 * Reporting success means queued, not a synchronous UI/PetCore update.
 * All identifiers refer to the command returned by take_game_command(). */
int pet_app_report_link(u32 session, u8 capabilities);
int pet_app_take_game_command(PetGameCommand *out);
int pet_app_report_game_started(u32 session, u32 id, u8 accepted);
int pet_app_report_game_progress(u32 session, u32 id, u8 round, u8 total, u8 step);
int pet_app_report_game_result(u32 session, u32 id, u8 result,
                               u8 had_input, u16 score, u32 reaction_ms);
int pet_app_report_game_cancelled(u32 session, u32 id);

#endif
