#include "pet_app.h"
#include "pet_ui.h"
#include "pet_pages.h"
#include "pet_imu.h"
#include "pet_motion.h"
#include "pet_flash.h"
#include <lvgl.h>
#include <string.h>
#include <stdlib.h>
#include "pet_logic/edgi/pet_balance.h"

enum { APP_HEAD = 1, APP_RESET, APP_HUG, APP_TEST_TOUCH, APP_LAYOUT,
       APP_NAV, APP_GAME_START, APP_GAME_CANCEL, APP_LINK, APP_STARTED,
       APP_PROGRESS, APP_RESULT, APP_CANCELLED, APP_DEMO, APP_MOTION,
       APP_SAVE, APP_TEST_DONE };
typedef struct {
    u8 kind, from_touch, action, a, b, c;
    u16 x, y, score;
    u32 session, id, time;
} AppEvent;
typedef PetAppView PublishedState;

static PetCore s_pet;
static struct rt_messagequeue s_queue;
static rt_uint32_t s_queue_pool[256];
static struct rt_messagequeue s_game_commands;
static rt_uint32_t s_command_pool[32];
static PetGame s_game;
static PetCore s_demo_backup;
static u32 s_demo_clicks, s_demo_hugs, s_demo_touch_hugs;
static u32 s_demo_light, s_demo_heavy, s_demo_physical_light, s_demo_physical_heavy;
static u32 s_demo_session = 0xd0000000UL;
static PublishedState s_published;
static PetSaved s_published_growth;
static rt_bool_t s_ready;
static u32 s_touch_clicks;
static u32 s_hugs, s_touch_hugs;
static u32 s_light, s_heavy, s_physical_light, s_physical_heavy;
static u8 s_imu_ready;
static u32 s_feedback_at;
static u32 s_feedback_ms;
static u8 s_feedback;
static u32 s_save_at;
static u8 s_diagnostic;
static PetCore s_diagnostic_backup;
static PublishedState s_diagnostic_view;

static int begin_diagnostic(void)
{
    if (s_diagnostic) return 1;
    if (s_game.connected && !s_game.demo) {
        rt_kprintf("[A4] diagnostic input rejected while real 51 is connected\n"); return 0;
    }
    s_diagnostic_backup = s_game.demo ? s_demo_backup : s_pet;
    s_diagnostic_view = s_published;
    s_diagnostic_view.physical_clicks = s_touch_clicks;
    s_diagnostic_view.hugs = s_hugs; s_diagnostic_view.physical_hugs = s_touch_hugs;
    s_diagnostic_view.light_shakes = s_light; s_diagnostic_view.heavy_shakes = s_heavy;
    s_diagnostic_view.physical_light = s_physical_light;
    s_diagnostic_view.physical_heavy = s_physical_heavy;
    if (s_game.demo) {
        s_diagnostic_view.physical_clicks = s_demo_clicks;
        s_diagnostic_view.hugs = s_demo_hugs; s_diagnostic_view.physical_hugs = s_demo_touch_hugs;
        s_diagnostic_view.light_shakes = s_demo_light; s_diagnostic_view.heavy_shakes = s_demo_heavy;
        s_diagnostic_view.physical_light = s_demo_physical_light;
        s_diagnostic_view.physical_heavy = s_demo_physical_heavy;
    }
    s_diagnostic = 1;
    rt_kprintf("[A4] diagnostic sandbox: changes will not be saved\n");
    return 1;
}

static void publish(const PetSnapshot *snapshot)
{
    PetStoreStatus store;
    int growth_pending = pet_store_growth_pending(&s_pet.saved);
    pet_store_get(&store);
    rt_base_t level = rt_hw_interrupt_disable();
    s_published.pet = *snapshot;
    s_published_growth = s_pet.saved;
    s_published.interactions = s_pet.saved.interactions;
    s_published.physical_clicks = s_touch_clicks;
    s_published.hugs = s_hugs;
    s_published.physical_hugs = s_touch_hugs;
    s_published.feedback = s_feedback;
    s_published.game = s_game;
    s_published.games = s_pet.saved.games;
    s_published.wins = s_pet.saved.wins;
    s_published.best_reaction_ms = s_pet.saved.best_reaction_ms;
    s_published.page = pet_pages_current();
    s_published.motion_ready = s_imu_ready;
    s_published.light_shakes = s_light; s_published.heavy_shakes = s_heavy;
    s_published.physical_light = s_physical_light; s_published.physical_heavy = s_physical_heavy;
    s_published.store = store;
    s_published.diagnostic = s_diagnostic;
    s_published.growth_pending = (u8)growth_pending;
    rt_hw_interrupt_enable(level);
}

int pet_app_get_view(PetAppView *out)
{
    rt_base_t level;
    if (!out || !s_ready) return -RT_ERROR;
    level = rt_hw_interrupt_disable();
    *out = s_published;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

static int post_event(const AppEvent *event)
{
    if (!s_ready) return -RT_ERROR;
    return rt_mq_send(&s_queue, event, sizeof(*event));
}

int pet_app_navigate(u8 page)
{
    AppEvent event = {0};
    if (page > PET_PAGE_STATUS) return -RT_EINVAL;
    event.kind = APP_NAV; event.a = page;
    return post_event(&event);
}

int pet_app_request_game(u8 type)
{
    AppEvent event = {0};
    event.kind = APP_GAME_START; event.a = type;
    return post_event(&event);
}

int pet_app_cancel_game(void)
{
    AppEvent event = {0};
    event.kind = APP_GAME_CANCEL;
    return post_event(&event);
}

int pet_app_save(void)
{
    AppEvent event = {0};
    event.kind = APP_SAVE;
    return post_event(&event);
}

int pet_app_report_link(u32 session, u8 capabilities)
{
    AppEvent event = {0};
    event.kind = APP_LINK; event.session = session; event.a = capabilities;
    return post_event(&event);
}

int pet_app_take_game_command(PetGameCommand *out)
{
    PublishedState state;
    unsigned budget = 4;
    if (!out || pet_app_get_view(&state) != RT_EOK || state.game.demo) return -RT_ERROR;
    while (budget-- && rt_mq_recv(&s_game_commands, out, sizeof(*out), 0) == (rt_ssize_t)sizeof(*out)) {
        pet_app_get_view(&state);
        if (!state.game.connected || state.game.demo || out->session != state.game.session ||
            out->game_id != state.game.game_id) continue;
        if ((out->action == PET_GAME_SEND_START && state.game.phase == PET_GAME_STARTING) ||
            (out->action == PET_GAME_SEND_CANCEL && state.game.phase == PET_GAME_CANCELLING)) return RT_EOK;
    }
    return -RT_ERROR;
}

int pet_app_report_game_started(u32 session, u32 id, u8 accepted)
{
    AppEvent event = {0};
    if (!session || !id || accepted > 1) return -RT_EINVAL;
    event.kind = APP_STARTED; event.session = session; event.id = id; event.a = accepted;
    return post_event(&event);
}

int pet_app_report_game_progress(u32 session, u32 id, u8 round, u8 total, u8 step)
{
    AppEvent event = {0};
    if (!session || !id || !round || round > total || total != 4 || step > PET_GAME_INPUT) return -RT_EINVAL;
    event.kind = APP_PROGRESS; event.session = session; event.id = id;
    event.a = round; event.b = total; event.c = step;
    return post_event(&event);
}

int pet_app_report_game_result(u32 session, u32 id, u8 result,
                               u8 had_input, u16 score, u32 reaction_ms)
{
    AppEvent event = {0};
    if (!session || !id || result < GAME_WIN || result > GAME_TIMEOUT ||
        had_input > 1 || score > 4 || reaction_ms ||
        (result == GAME_WIN && (!had_input || score != 4))) return -RT_EINVAL;
    event.kind = APP_RESULT; event.session = session; event.id = id;
    event.a = result; event.b = had_input; event.score = score; event.time = reaction_ms;
    return post_event(&event);
}

int pet_app_report_game_cancelled(u32 session, u32 id)
{
    AppEvent event = {0};
    if (!session || !id) return -RT_EINVAL;
    event.kind = APP_CANCELLED; event.session = session; event.id = id;
    return post_event(&event);
}

static void end_demo(u32 now)
{
    if (!s_game.demo) return;
    s_pet = s_demo_backup;
    s_touch_clicks = s_demo_clicks; s_hugs = s_demo_hugs; s_touch_hugs = s_demo_touch_hugs;
    s_light = s_demo_light; s_heavy = s_demo_heavy;
    s_physical_light = s_demo_physical_light; s_physical_heavy = s_demo_physical_heavy;
    s_feedback = 0;
    pet_game_init(&s_game);
    pet_tick(&s_pet, now);
    rt_kprintf("[A2] demo ended; original pet restored\n");
}

int pet_app_get_snapshot(PetSnapshot *out)
{
    rt_base_t level;
    if (!out || !s_ready) return -RT_ERROR;
    level = rt_hw_interrupt_disable();
    *out = s_published.pet;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

static int post(u8 kind, rt_bool_t from_touch)
{
    AppEvent event;
    if (!s_ready) return -RT_ERROR;
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.from_touch = from_touch ? 1 : 0;
    return rt_mq_send(&s_queue, &event, sizeof(event));
}

int pet_app_post_head(rt_bool_t from_touch)
{
    return post(APP_HEAD, from_touch);
}

int pet_app_post_hug(rt_bool_t from_touch)
{
    return post(APP_HUG, from_touch);
}

int pet_app_post_motion(u8 motion, u32 sampled_at, rt_bool_t physical)
{
    AppEvent event = {0};
    if (motion != PET_MOTION_LIGHT && motion != PET_MOTION_HEAVY) return -RT_EINVAL;
    event.kind = APP_MOTION; event.a = motion; event.time = sampled_at;
    event.from_touch = physical ? 1 : 0;
    return post_event(&event);
}

static void tick(lv_timer_t *timer)
{
    AppEvent event;
    PetSnapshot snapshot;
    PetImuSample imu;
    u32 now = rt_tick_get_millisecond();
    unsigned int budget = 8;
    (void)timer;

    /* Process a bounded batch. Cooldowns use processing time monotonically;
     * queued older timestamps must not move the PetCore clock backwards. */
    while (budget-- && rt_mq_recv(&s_queue, &event, sizeof(event), 0) == (rt_ssize_t)sizeof(event)) {
        now = rt_tick_get_millisecond();
        if ((event.kind == APP_TEST_TOUCH ||
             ((event.kind == APP_HEAD || event.kind == APP_HUG || event.kind == APP_MOTION) && !event.from_touch)) &&
            !begin_diagnostic()) continue;
        if (event.kind == APP_RESET) {
            if (s_game.connected || pet_game_active(&s_game) || s_game.demo) {
                rt_kprintf("[A2] reset rejected while link or game is active\n");
                continue;
            }
            if (!begin_diagnostic()) continue;
            pet_ui_reset_input();
            pet_init(&s_pet, RT_NULL, now);
            pet_game_init(&s_game);
            pet_pages_reset();
            s_touch_clicks = 0;
            s_hugs = s_touch_hugs = 0;
            s_light = s_heavy = s_physical_light = s_physical_heavy = 0;
            s_feedback = 0;
            rt_kprintf("[A1] reset to defaults\n");
        } else if (event.kind == APP_HEAD || event.kind == APP_HUG) {
            int accepted;
            int hug = event.kind == APP_HUG;
            if (pet_game_active(&s_game)) continue;
            if (event.from_touch) {
                if (hug) s_touch_hugs++;
                else s_touch_clicks++;
            }
            accepted = pet_input(&s_pet, hug ? INPUT_HUG : INPUT_HEAD, now);
            if (accepted && hug) s_hugs++;
            s_feedback = accepted ? (hug ? PET_FEEDBACK_HUG : PET_FEEDBACK_HEAD) : PET_FEEDBACK_COOLDOWN;
            s_feedback_at = now;
            s_feedback_ms = accepted && hug ? PET_HUG_REACTION_MS : 1000UL;
            rt_kprintf("[A1] %s source=%s accepted=%d mood=%u trust_x100=%u inputs=%lu clicks=%lu hugs=%lu\n",
                       hug ? "hug" : "head", event.from_touch ? "touch" : "test", accepted,
                       (unsigned)s_pet.mood, (unsigned)s_pet.saved.trust_x100,
                       (unsigned long)s_pet.saved.interactions, (unsigned long)s_touch_clicks,
                       (unsigned long)s_hugs);
        } else if (event.kind == APP_MOTION) {
            int accepted, heavy = event.a == PET_MOTION_HEAVY;
            /* A queued physical gesture is not allowed to appear much later.
             * Game demos ignore real motion to preserve their saved baseline. */
            if ((u32)(now - event.time) > 500UL) {
                rt_kprintf("[A3] stale motion ignored\n"); continue;
            }
            if (s_game.demo && event.from_touch) continue;
            if (event.from_touch) {
                if (heavy) s_physical_heavy++;
                else s_physical_light++;
            }
            if (!heavy && (pet_game_active(&s_game) || s_pet.ai_id)) continue;
            if (heavy) {
                pet_ui_reset_input();
                if (pet_game_cancel(&s_game, &s_pet, now)) s_game.error = PET_GAME_MOTION_INTERRUPTED;
                s_feedback = 0;
            }
            accepted = pet_input(&s_pet, heavy ? INPUT_HEAVY : INPUT_LIGHT, now);
            if (accepted) {
                if (heavy) s_heavy++;
                else {
                    s_light++;
                    s_feedback = PET_FEEDBACK_LIGHT;
                    s_feedback_at = now; s_feedback_ms = 1200UL;
                }
            }
            rt_kprintf("[A3] motion=%s source=%s accepted=%d mood=%u energy=%u trust_x100=%u\n",
                       heavy ? "heavy" : "light", event.from_touch ? "sensor" : "test",
                       accepted, s_pet.mood, s_pet.energy, s_pet.saved.trust_x100);
        } else if (event.kind == APP_TEST_TOUCH) {
            int result = pet_ui_test_touch(event.action, event.x, event.y);
            rt_kprintf("[A1] pointer action=%u x=%u y=%u result=%d\n",
                       event.action, event.x, event.y, result);
        } else if (event.kind == APP_LAYOUT) {
            pet_ui_log_layout();
            pet_pages_log_layout();
        } else if (event.kind == APP_NAV) {
            if (!pet_game_active(&s_game) || event.a == PET_PAGE_GAMES)
                pet_pages_show(event.a);
        } else if (event.kind == APP_GAME_START) {
            pet_pages_show(PET_PAGE_GAMES);
            pet_game_start(&s_game, &s_pet, event.a, now);
        } else if (event.kind == APP_GAME_CANCEL) {
            pet_game_cancel(&s_game, &s_pet, now);
        } else if (event.kind == APP_LINK) {
            if (s_game.demo) end_demo(now);
            pet_game_link(&s_game, &s_pet, event.session, event.a, 0, now);
        } else if (event.kind == APP_STARTED) {
            pet_game_started(&s_game, &s_pet, event.session, event.id, event.a, now);
        } else if (event.kind == APP_PROGRESS) {
            pet_game_progress(&s_game, event.session, event.id, event.a, event.b, event.c);
        } else if (event.kind == APP_RESULT) {
            if (pet_game_result(&s_game, &s_pet, event.session, event.id,
                                event.a, event.b, event.score, event.time, now)) s_feedback = 0;
        } else if (event.kind == APP_CANCELLED) {
            pet_game_cancelled(&s_game, event.session, event.id);
        } else if (event.kind == APP_DEMO) {
            if (event.a && !s_game.connected && !pet_game_active(&s_game) && !s_game.demo) {
                s_demo_backup = s_pet;
                s_demo_clicks = s_touch_clicks; s_demo_hugs = s_hugs; s_demo_touch_hugs = s_touch_hugs;
                s_demo_light = s_light; s_demo_heavy = s_heavy;
                s_demo_physical_light = s_physical_light; s_demo_physical_heavy = s_physical_heavy;
                pet_game_link(&s_game, &s_pet, ++s_demo_session, PET_GAME_CAP_SIMON, 1, now);
                rt_kprintf("[A2] demo enabled; simulated 51, no UART traffic\n");
            } else if (!event.a) end_demo(now);
        } else if (event.kind == APP_SAVE) {
            if (!s_diagnostic && !s_game.demo) {
                pet_tick(&s_pet, now);
                if (pet_store_request(&s_pet.saved) == RT_EOK) s_save_at = now;
            } else rt_kprintf("[A4] save blocked in diagnostic/demo mode\n");
        } else if (event.kind == APP_TEST_DONE && s_diagnostic) {
            end_demo(now);
            s_pet = s_diagnostic_backup;
            s_touch_clicks = s_diagnostic_view.physical_clicks;
            s_hugs = s_diagnostic_view.hugs; s_touch_hugs = s_diagnostic_view.physical_hugs;
            s_light = s_diagnostic_view.light_shakes; s_heavy = s_diagnostic_view.heavy_shakes;
            s_physical_light = s_diagnostic_view.physical_light;
            s_physical_heavy = s_diagnostic_view.physical_heavy;
            pet_game_init(&s_game); pet_ui_reset_input(); pet_pages_reset();
            s_feedback = 0; s_diagnostic = 0; s_save_at = now;
            rt_kprintf("[A4] diagnostic ended; real pet restored\n");
        }
    }
    pet_imu_get(&imu);
    now = rt_tick_get_millisecond();
    s_imu_ready = imu.ready && (u32)(now - imu.at) <= 150UL;
    pet_imu_stable(&s_pet, s_imu_ready && imu.stable, now);
    pet_game_tick(&s_game, &s_pet, now);
    /* Powered-time accounting changes even without an emotion revision. */
    if (!s_diagnostic && !s_game.demo && (u32)(now - s_save_at) >= 30000UL && !pet_store_busy()) {
        s_save_at = now;
        pet_store_request(&s_pet.saved);
    }
    if (s_game.command_pending) {
        if (s_game.demo) {
            rt_kprintf("[A2_DEMO_COMMAND] action=%u session=%lu id=%lu\n",
                       s_game.command.action, (unsigned long)s_game.command.session,
                       (unsigned long)s_game.command.game_id);
            s_game.command_pending = 0;
        } else if (rt_mq_send(&s_game_commands, &s_game.command, sizeof(s_game.command)) == RT_EOK)
            s_game.command_pending = 0;
    }
    if (s_feedback && (u32)(now - s_feedback_at) >= s_feedback_ms) s_feedback = 0;
    pet_snapshot(&s_pet, &snapshot);
    publish(&snapshot);
    pet_ui_update(&snapshot, s_feedback, now);
    pet_pages_update(&s_published, now);
}

int pet_app_init(void)
{
    PetSnapshot snapshot;
    PetSaved restored;
    int loaded;
    lv_timer_t *timer;
    u32 now = rt_tick_get_millisecond();
    if (s_ready) return -RT_EBUSY;
    if (rt_mq_init(&s_queue, "pet_evt", s_queue_pool, sizeof(AppEvent),
                   sizeof(s_queue_pool), RT_IPC_FLAG_FIFO) != RT_EOK) return -RT_ERROR;
    if (rt_mq_init(&s_game_commands, "game_tx", s_command_pool, sizeof(PetGameCommand),
                   sizeof(s_command_pool), RT_IPC_FLAG_FIFO) != RT_EOK) {
        rt_mq_detach(&s_queue);
        return -RT_ERROR;
    }
    loaded = pet_store_init(&restored);
    now = rt_tick_get_millisecond();
    pet_init(&s_pet, loaded == 1 ? &restored : RT_NULL, now);
    s_save_at = now;
    pet_game_init(&s_game);
    pet_pages_create();
    timer = lv_timer_create(tick, 50, RT_NULL);
    if (!timer) {
        rt_mq_detach(&s_queue);
        rt_mq_detach(&s_game_commands);
        return -RT_ENOMEM;
    }
    pet_snapshot(&s_pet, &snapshot);
    publish(&snapshot);
    pet_ui_update(&snapshot, 0, now);
    pet_pages_update(&s_published, now);
    pet_ui_log_layout();
    s_ready = RT_TRUE;
    rt_kprintf("[A1] pet page ready; owner=LVGL; snapshot_bytes=%u core_bytes=%u\n",
               (unsigned)sizeof(PetSnapshot), (unsigned)sizeof(PetCore));
    return RT_EOK;
}

static int cmd_pet_status(int argc, char **argv)
{
    PublishedState state;
    rt_base_t level;
    (void)argc; (void)argv;
    if (!s_ready) { rt_kprintf("[A1] not ready\n"); return -1; }
    level = rt_hw_interrupt_disable();
    state = s_published;
    rt_hw_interrupt_enable(level);
    rt_kprintf("A1_STATUS mood=%u energy=%u trust_x100=%u base=%u display=%u rev=%lu inputs=%lu clicks=%lu feedback=%u hugs=%lu touch_hugs=%lu\n",
               (unsigned)state.pet.mood, (unsigned)state.pet.energy,
               (unsigned)state.pet.trust_x100, (unsigned)state.pet.base,
               (unsigned)state.pet.display, (unsigned long)state.pet.revision,
               (unsigned long)state.interactions, (unsigned long)state.physical_clicks,
               (unsigned)state.feedback, (unsigned long)state.hugs,
               (unsigned long)state.physical_hugs);
    rt_kprintf("A2_GAME page=%u connected=%u demo=%u session=%lu id=%lu phase=%u error=%u round=%u total=%u step=%u result=%u score=%lu games=%lu wins=%lu exp=%lu\n",
               state.page, state.game.connected, state.game.demo,
               (unsigned long)state.game.session, (unsigned long)state.game.game_id,
               state.game.phase, state.game.error, state.game.round, state.game.total,
               state.game.step, state.game.result, (unsigned long)state.game.score,
               (unsigned long)state.games, (unsigned long)state.wins, (unsigned long)state.pet.exp);
    rt_kprintf("A3_MOTION ready=%u light=%lu heavy=%lu physical_light=%lu physical_heavy=%lu\n",
               state.motion_ready, (unsigned long)state.light_shakes, (unsigned long)state.heavy_shakes,
               (unsigned long)state.physical_light, (unsigned long)state.physical_heavy);
    rt_kprintf("A4_STORE ready=%u loaded=%u gen=%lu pending=%u saving=%u writes=%lu failures=%lu error=%d damaged=%lu diagnostic=%u dirty=%u saved_trust=%u parks=%lu\n",
               state.store.ready, state.store.loaded, (unsigned long)state.store.generation,
               state.store.pending, state.store.saving, (unsigned long)state.store.writes,
               (unsigned long)state.store.failures, state.store.error, (unsigned long)state.store.damaged,
               state.diagnostic, state.growth_pending, state.store.saved_trust, pet_flash_parks());
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_pet_status, pet_status, Read the published pet snapshot);

static int cmd_growth(int argc, char **argv)
{
    PetSaved growth;
    rt_base_t level;
    (void)argc; (void)argv;
    if (!s_ready) return -RT_ERROR;
    level = rt_hw_interrupt_disable(); growth = s_published_growth; rt_hw_interrupt_enable(level);
    rt_kprintf("A4_GROWTH trust=%u quota=%u r0=%u r1=%u r2=%u r3=%u r4=%u window=%lu exp=%lu interactions=%lu games=%lu wins=%lu dizzy=%lu best=%lu\n",
               growth.trust_x100, growth.quota_used, growth.reward_count[0], growth.reward_count[1],
               growth.reward_count[2], growth.reward_count[3], growth.reward_count[4],
               (unsigned long)growth.window_ms, (unsigned long)growth.exp,
               (unsigned long)growth.interactions, (unsigned long)growth.games, (unsigned long)growth.wins,
               (unsigned long)growth.dizzy_count, (unsigned long)growth.best_reaction_ms);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_growth, pet_growth, Read the complete published growth ledger without saving);

static int cmd_save(int argc, char **argv)
{
    (void)argc; (void)argv;
    return pet_app_save();
}
MSH_CMD_EXPORT_ALIAS(cmd_save, pet_save, Save real growth data now and verify it);

static int cmd_test_done(int argc, char **argv)
{
    AppEvent event = {0};
    (void)argc; (void)argv;
    event.kind = APP_TEST_DONE;
    return post_event(&event);
}
MSH_CMD_EXPORT_ALIAS(cmd_test_done, pet_test_done, End diagnostic sandbox and restore the real pet);

static int cmd_pet_head(int argc, char **argv)
{
    int result;
    (void)argc; (void)argv;
    result = pet_app_post_head(RT_FALSE);
    rt_kprintf("[A1] test head queued=%d\n", result == RT_EOK);
    return result;
}
MSH_CMD_EXPORT_ALIAS(cmd_pet_head, pet_head, Queue one test head tap);

static int cmd_pet_hug(int argc, char **argv)
{
    (void)argc; (void)argv;
    return pet_app_post_hug(RT_FALSE);
}
MSH_CMD_EXPORT_ALIAS(cmd_pet_hug, pet_hug, Queue one recognized test hug);

static int cmd_pet_layout(int argc, char **argv)
{
    (void)argc; (void)argv;
    return post(APP_LAYOUT, RT_FALSE);
}
MSH_CMD_EXPORT_ALIAS(cmd_pet_layout, pet_layout, Read visible layout and touch bounds);

static int cmd_pet_touch(int argc, char **argv)
{
    AppEvent event;
    long x, y;
    char *end_x, *end_y;
    if (!s_ready || argc < 2) return -RT_EINVAL;
    memset(&event, 0, sizeof(event));
    event.kind = APP_TEST_TOUCH;
    if (argc == 2 && !strcmp(argv[1], "up")) event.action = PET_TEST_TOUCH_UP;
    else if (argc == 4 && (!strcmp(argv[1], "down") || !strcmp(argv[1], "move"))) {
        x = strtol(argv[2], &end_x, 10);
        y = strtol(argv[3], &end_y, 10);
        if (!*argv[2] || !*argv[3] || *end_x || *end_y ||
            x < 0 || x >= 480 || y < 0 || y >= 800) return -RT_EINVAL;
        event.action = !strcmp(argv[1], "down") ? PET_TEST_TOUCH_DOWN : PET_TEST_TOUCH_MOVE;
        event.x = (u16)x; event.y = (u16)y;
    } else return -RT_EINVAL;
    return rt_mq_send(&s_queue, &event, sizeof(event));
}
MSH_CMD_EXPORT_ALIAS(cmd_pet_touch, pet_touch, Diagnostic pointer down or move x y and up);

static int cmd_pet_reset(int argc, char **argv)
{
    int result;
    (void)argc; (void)argv;
    result = post(APP_RESET, RT_FALSE);
    rt_kprintf("[A1] reset queued=%d\n", result == RT_EOK);
    return result;
}
MSH_CMD_EXPORT_ALIAS(cmd_pet_reset, pet_reset, Reset the A1 demo via its owner thread);

static int cmd_pet_nav(int argc, char **argv)
{
    if (argc != 2) return -RT_EINVAL;
    if (!strcmp(argv[1], "home")) return pet_app_navigate(PET_PAGE_HOME);
    if (!strcmp(argv[1], "games")) return pet_app_navigate(PET_PAGE_GAMES);
    if (!strcmp(argv[1], "status")) return pet_app_navigate(PET_PAGE_STATUS);
    return -RT_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(cmd_pet_nav, pet_nav, Navigate home games or status through the owner);

static int cmd_pet_game_demo(int argc, char **argv)
{
    PublishedState state;
    AppEvent event = {0};
    char *end;
    long round;
    if (argc < 2 || pet_app_get_view(&state) != RT_EOK) return -RT_EINVAL;
    if (argc == 2 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"))) {
        event.kind = APP_DEMO; event.a = !strcmp(argv[1], "on");
        return post_event(&event);
    }
    if (!state.game.demo) return -RT_ERROR;
    if (argc == 2 && !strcmp(argv[1], "ack"))
        return pet_app_report_game_started(state.game.session, state.game.game_id, 1);
    if (argc == 2 && !strcmp(argv[1], "reject"))
        return pet_app_report_game_started(state.game.session, state.game.game_id, 0);
    if (argc == 2 && !strcmp(argv[1], "cancelled"))
        return pet_app_report_game_cancelled(state.game.session, state.game.game_id);
    if (argc == 2 && !strcmp(argv[1], "win"))
        return pet_app_report_game_result(state.game.session, state.game.game_id, GAME_WIN, 1, 4, 0);
    if (argc == 2 && !strcmp(argv[1], "fail"))
        return pet_app_report_game_result(state.game.session, state.game.game_id, GAME_FAIL, 1, 1, 0);
    if (argc == 3 && !strcmp(argv[1], "step")) {
        round = strtol(argv[2], &end, 10);
        if (!*argv[2] || *end || round < 1 || round > 4) return -RT_EINVAL;
        return pet_app_report_game_progress(state.game.session, state.game.game_id,
                                            (u8)round, 4, PET_GAME_INPUT);
    }
    return -RT_EINVAL;
}
MSH_CMD_EXPORT_ALIAS(cmd_pet_game_demo, pet_game_demo, Simulated 51 only: on off ack reject step N win fail cancelled);

static int cmd_motion_test(int argc, char **argv)
{
    u8 motion;
    if (argc != 2) return -RT_EINVAL;
    if (!strcmp(argv[1], "light")) motion = PET_MOTION_LIGHT;
    else if (!strcmp(argv[1], "heavy")) motion = PET_MOTION_HEAVY;
    else return -RT_EINVAL;
    return pet_app_post_motion(motion, rt_tick_get_millisecond(), RT_FALSE);
}
MSH_CMD_EXPORT_ALIAS(cmd_motion_test, pet_motion_test, Inject a recognized test gesture light or heavy);
