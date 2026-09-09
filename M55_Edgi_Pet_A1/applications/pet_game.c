#include "pet_game.h"
#include <string.h>

int pet_game_active(const PetGame *g)
{
    return g->phase == PET_GAME_STARTING || g->phase == PET_GAME_PLAYING ||
           g->phase == PET_GAME_CANCELLING;
}

static int matches(const PetGame *g, u32 session, u32 id)
{
    return g->connected && session && session == g->session && id && id == g->game_id;
}

static void close_core(PetGame *g, PetCore *pet, u32 now)
{
    if (pet->active_game == g->game_id)
        pet_finish_game(pet, g->game_id, GAME_CANCEL, 0, 0, now);
}

static void command(PetGame *g, u8 action, u32 now)
{
    g->command.action = action;
    g->command.game_type = PET_GAME_SIMON;
    g->command.session = g->session;
    g->command.game_id = g->game_id;
    g->command.seed = now ^ g->game_id ^ g->session;
    g->command_pending = 1;
}

void pet_game_init(PetGame *g) { memset(g, 0, sizeof(*g)); }

void pet_game_link(PetGame *g, PetCore *pet, u32 session, u8 capabilities, u8 demo, u32 now)
{
    if (session && session == g->blocked_session) return;
    /* A repeated notification for this connection must not restart a game. */
    if (session != g->session || demo != g->demo ||
        (pet_game_active(g) && !(capabilities & PET_GAME_CAP_SIMON))) {
        if (pet_game_active(g)) {
            close_core(g, pet, now);
            g->phase = PET_GAME_ERROR;
            g->error = PET_GAME_LINK_LOST;
        }
        g->command_pending = 0;
    }
    g->session = session;
    g->connected = session != 0;
    g->capabilities = session ? capabilities & PET_GAME_CAP_SIMON : 0;
    g->demo = session ? demo : 0;
}

int pet_game_start(PetGame *g, PetCore *pet, u8 type, u32 now)
{
    u32 id;
    if (pet_game_active(g)) return 0;
    g->error = PET_GAME_OK;
    if (!g->connected) g->error = PET_GAME_NO_LINK;
    else if (type != PET_GAME_SIMON || !(g->capabilities & PET_GAME_CAP_SIMON))
        g->error = PET_GAME_UNSUPPORTED;
    else if (pet->sleeping || pet->heavy_latched || pet->active_game || pet->ai_id ||
             pet->energy < 20 || pet->last_game == 0xffffffffUL) g->error = PET_GAME_BUSY;
    if (g->error) { g->phase = PET_GAME_ERROR; return 0; }
    id = pet->last_game + 1;
    if (!pet_begin_game(pet, id, now)) {
        g->phase = PET_GAME_ERROR; g->error = PET_GAME_BUSY; return 0;
    }
    g->game_id = id;
    g->phase = PET_GAME_STARTING;
    g->phase_at = now;
    g->round = 0; g->total = 4; g->step = PET_GAME_WATCH;
    g->result = 0; g->score = 0; g->reaction_ms = 0;
    command(g, PET_GAME_SEND_START, now);
    return 1;
}

int pet_game_started(PetGame *g, PetCore *pet, u32 session, u32 id, u8 accepted, u32 now)
{
    if (!matches(g, session, id) || g->phase != PET_GAME_STARTING || accepted > 1) return 0;
    g->command_pending = 0;
    if (!accepted) {
        close_core(g, pet, now);
        g->phase = PET_GAME_ERROR; g->error = PET_GAME_REJECTED;
    } else {
        g->phase = PET_GAME_PLAYING; g->phase_at = now;
        /* Waiting for the start ACK does not count towards reward eligibility. */
        pet->game_at = now;
        g->round = 1;
    }
    return 1;
}

int pet_game_progress(PetGame *g, u32 session, u32 id, u8 round, u8 total, u8 step)
{
    if (!matches(g, session, id) || g->phase != PET_GAME_PLAYING ||
        total != 4 || round < 1 || round > total || round < g->round ||
        step > PET_GAME_INPUT || (round == g->round && step < g->step)) return 0;
    g->round = round; g->total = total; g->step = step;
    return 1;
}

int pet_game_result(PetGame *g, PetCore *pet, u32 session, u32 id,
                    u8 result, u8 had_input, u16 score, u32 reaction_ms, u32 now)
{
    if (!matches(g, session, id) || g->phase != PET_GAME_PLAYING ||
        result < GAME_WIN || result > GAME_TIMEOUT || had_input > 1 || score > 4 ||
        reaction_ms || (result == GAME_WIN && (!had_input || score != 4))) return 0;
    if (!pet_finish_game(pet, id, result, had_input, reaction_ms, now)) return 0;
    g->phase = PET_GAME_FINISHED; g->result = result;
    g->score = score; g->reaction_ms = reaction_ms;
    g->command_pending = 0;
    return 1;
}

int pet_game_cancel(PetGame *g, PetCore *pet, u32 now)
{
    if (g->phase != PET_GAME_STARTING && g->phase != PET_GAME_PLAYING) return 0;
    close_core(g, pet, now);
    g->phase = PET_GAME_CANCELLING; g->phase_at = now;
    g->result = GAME_CANCEL;
    command(g, PET_GAME_SEND_CANCEL, now);
    return 1;
}

int pet_game_cancelled(PetGame *g, u32 session, u32 id)
{
    if (!matches(g, session, id) || g->phase != PET_GAME_CANCELLING) return 0;
    g->command_pending = 0;
    g->phase = g->error ? PET_GAME_ERROR : PET_GAME_FINISHED;
    g->result = GAME_CANCEL;
    return 1;
}

void pet_game_tick(PetGame *g, PetCore *pet, u32 now)
{
    u32 elapsed = (u32)(now - g->phase_at);
    if (g->phase == PET_GAME_STARTING && elapsed >= 5000UL) {
        pet_game_cancel(g, pet, now);
        g->error = PET_GAME_START_TIMEOUT;
    } else if (g->phase == PET_GAME_PLAYING &&
               (pet->active_game != g->game_id || elapsed >= 120000UL)) {
        pet_game_cancel(g, pet, now);
        g->error = PET_GAME_RUN_TIMEOUT;
    } else if (g->phase == PET_GAME_CANCELLING && elapsed >= 3000UL) {
        /* Remote ownership is uncertain. Require a new handshake before play. */
        g->phase = PET_GAME_ERROR;
        if (!g->error) g->error = PET_GAME_CANCEL_TIMEOUT;
        g->blocked_session = g->session;
        g->connected = 0; g->session = 0; g->capabilities = 0;
        g->command_pending = 0;
    }
}
