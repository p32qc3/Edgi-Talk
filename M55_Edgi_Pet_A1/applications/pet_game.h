#ifndef PET_GAME_H
#define PET_GAME_H
#include "pet_logic/edgi/pet_core.h"

/* A-owned game coordinator; one LVGL owner calls every function. B exchanges
 * commands/events through pet_app.h, never writes this object or PetCore. */
#define PET_GAME_SIMON 1U
#define PET_GAME_CAP_SIMON 1U
enum PetGamePhase { PET_GAME_IDLE, PET_GAME_STARTING, PET_GAME_PLAYING,
                     PET_GAME_CANCELLING, PET_GAME_FINISHED, PET_GAME_ERROR };
enum PetGameError { PET_GAME_OK, PET_GAME_NO_LINK, PET_GAME_UNSUPPORTED,
                     PET_GAME_BUSY, PET_GAME_REJECTED, PET_GAME_START_TIMEOUT,
                     PET_GAME_LINK_LOST, PET_GAME_CANCEL_TIMEOUT, PET_GAME_RUN_TIMEOUT,
                     PET_GAME_MOTION_INTERRUPTED };
enum PetGameAction { PET_GAME_SEND_START = 1, PET_GAME_SEND_CANCEL };
enum PetGameStep { PET_GAME_WATCH, PET_GAME_INPUT };
typedef struct {
    u32 session, game_id, seed;
    u8 action, game_type;
} PetGameCommand;
typedef struct {
    u32 session, game_id, phase_at, score, reaction_ms, blocked_session;
    u8 connected, capabilities, demo, phase, error;
    u8 round, total, step, result;
    u8 command_pending;
    PetGameCommand command;
} PetGame;

void pet_game_init(PetGame *g);
void pet_game_link(PetGame *g, PetCore *pet, u32 session, u8 capabilities, u8 demo, u32 now);
int pet_game_start(PetGame *g, PetCore *pet, u8 type, u32 now);
int pet_game_started(PetGame *g, PetCore *pet, u32 session, u32 id, u8 accepted, u32 now);
int pet_game_progress(PetGame *g, u32 session, u32 id, u8 round, u8 total, u8 step);
int pet_game_result(PetGame *g, PetCore *pet, u32 session, u32 id,
                    u8 result, u8 had_input, u16 score, u32 reaction_ms, u32 now);
int pet_game_cancel(PetGame *g, PetCore *pet, u32 now);
int pet_game_cancelled(PetGame *g, u32 session, u32 id);
void pet_game_tick(PetGame *g, PetCore *pet, u32 now);
int pet_game_active(const PetGame *g);
#endif
