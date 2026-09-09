#include "pet_pages.h"
#include "pet_ui.h"
#include "comm_ai/pet_service_status.h"
#include <lvgl.h>
#include <string.h>
#include <stdint.h>

LV_FONT_DECLARE(pet_font_22);
LV_FONT_DECLARE(pet_font_30);
LV_FONT_DECLARE(pet_font_36);
#define BG 0xF7F5EC
#define INK 0x354A41
#define MUTED 0x738278
#define GREEN 0x83A992
#define PAPER 0xFFFDF6

typedef struct {
    lv_obj_t *root, *page[3], *nav[3], *nav_text[3];
    lv_obj_t *catalog, *detail, *game_title, *game_caption, *link_text, *catalog_hint;
    lv_obj_t *game_primary, *primary_text, *game_back, *detail_title, *detail_body, *detail_hint;
    lv_obj_t *mini_mood, *mini_trust, *mini_state, *mini_eyes[2], *mini_closed[2];
    lv_obj_t *mini_dizzy[2], *status_caption, *service_status;
    lv_obj_t *status_values[3], *status_rows[5];
    lv_obj_t *save_hint, *save_button, *save_caption;
    u8 current, detail_open, locked, nav_drawn, last_page, last_locked;
} PetPages;
static PetPages s_pages;

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                        const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y); lv_obj_set_width(obj, w);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, text);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void set_text(lv_obj_t *obj, const char *text)
{
    if (strcmp(lv_label_get_text(obj), text)) lv_label_set_text(obj, text);
}

static void show(lv_obj_t *obj, int visible)
{
    if (visible && lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else if (!visible && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void enable(lv_obj_t *obj, int enabled)
{
    if (enabled && lv_obj_has_state(obj, LV_STATE_DISABLED)) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else if (!enabled && !lv_obj_has_state(obj, LV_STATE_DISABLED)) lv_obj_add_state(obj, LV_STATE_DISABLED);
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                         uint32_t color, lv_event_cb_t callback, void *data, lv_obj_t **caption)
{
    lv_obj_t *obj = box(parent, x, y, w, h, color, 18);
    lv_obj_t *word = label(obj, text, 8, (h - 30) / 2, w - 16, &pet_font_22, INK);
    lv_obj_set_style_text_align(word, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xC5D8C7), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xE6E7DE), LV_STATE_DISABLED);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
    if (caption) *caption = word;
    return obj;
}

static const char *emotion(u8 value)
{
    switch (value) {
    case PET_HAPPY: return "开心";
    case PET_SAD: return "待陪伴";
    case PET_ANGRY: return "生气";
    case PET_DIZZY: return "头晕";
    case PET_SLEEP: return "睡觉";
    case PET_THINKING: return "思考中";
    case PET_EXCITED: return "兴奋";
    default: return "自在";
    }
}

static void nav_clicked(lv_event_t *event)
{
    u8 page = (u8)(uintptr_t)lv_event_get_user_data(event);
    if (pet_app_navigate(page) != RT_EOK) rt_kprintf("[A2] navigation queue full\n");
}

static void save_clicked(lv_event_t *event)
{
    (void)event;
    if (pet_app_save() != RT_EOK) rt_kprintf("[A4] save request queue full\n");
}

static void detail_clicked(lv_event_t *event)
{
    (void)event;
    s_pages.detail_open = 1;
    show(s_pages.catalog, 0); show(s_pages.detail, 1);
    set_text(s_pages.game_title, "记忆挑战");
    set_text(s_pages.game_caption, "看灯光，记顺序，和我一起闯关");
}

static void back_clicked(lv_event_t *event)
{
    (void)event;
    if (s_pages.locked) return;
    s_pages.detail_open = 0;
    show(s_pages.detail, 0); show(s_pages.catalog, 1);
    set_text(s_pages.game_title, "一起玩游戏");
    set_text(s_pages.game_caption, "选一个喜欢的，陪我玩一会儿");
}

static void primary_clicked(lv_event_t *event)
{
    PetAppView view;
    int result;
    (void)event;
    if (pet_app_get_view(&view) != RT_EOK) return;
    if (view.game.phase == PET_GAME_STARTING || view.game.phase == PET_GAME_PLAYING)
        result = pet_app_cancel_game();
    else result = pet_app_request_game(PET_GAME_SIMON);
    if (result != RT_EOK) rt_kprintf("[A2] game request queue full\n");
}

static void create_mini(lv_obj_t *parent)
{
    lv_obj_t *head;
    unsigned i;
    box(parent, 30, 122, 99, 9, 0xCCDCCE, 5);
    box(parent, 49, 91, 63, 40, 0xFFFBED, 22);
    box(parent, 36, 19, 26, 54, 0xFFFBED, 14);
    box(parent, 98, 19, 26, 54, 0xFFFBED, 14);
    box(parent, 43, 27, 12, 31, 0xEFC2AA, 6);
    box(parent, 105, 27, 12, 31, 0xEFC2AA, 6);
    head = box(parent, 25, 53, 112, 67, 0xFFFBED, 34);
    for (i = 0; i < 2; i++) {
        int x = i ? 73 : 29;
        s_pages.mini_eyes[i] = box(head, x, 23, 9, 14, INK, 5);
        s_pages.mini_closed[i] = box(head, x - 1, 30, 12, 4, INK, 2);
        show(s_pages.mini_closed[i], 0);
        s_pages.mini_dizzy[i] = box(head, x - 1, 25, 12, 12, PAPER, 0);
        lv_obj_set_style_bg_opa(s_pages.mini_dizzy[i], LV_OPA_TRANSP, 0);
        box(s_pages.mini_dizzy[i], 4, 0, 3, 12, INK, 1);
        box(s_pages.mini_dizzy[i], 0, 4, 12, 3, INK, 1);
        lv_obj_set_style_transform_pivot_x(s_pages.mini_dizzy[i], 6, 0);
        lv_obj_set_style_transform_pivot_y(s_pages.mini_dizzy[i], 6, 0);
        lv_obj_set_style_transform_rotation(s_pages.mini_dizzy[i], 450, 0);
        show(s_pages.mini_dizzy[i], 0);
    }
    box(head, 15, 40, 16, 7, 0xEFC2AA, 4);
    box(head, 83, 40, 16, 7, 0xEFC2AA, 4);
    box(head, 54, 38, 6, 4, 0xA78068, 2);
    box(head, 51, 47, 12, 3, INK, 2);
}

static void create_games(lv_obj_t *page)
{
    lv_obj_t *pill, *card;
    unsigned i;
    label(page, "一起陪伴", 32, 26, 200, &pet_font_22, MUTED);
    s_pages.game_title = label(page, "一起玩游戏", 32, 62, 416, &pet_font_30, INK);
    s_pages.game_caption = label(page, "选一个喜欢的，陪我玩一会儿", 32, 112, 416, &pet_font_22, MUTED);
    pill = box(page, 284, 27, 164, 34, 0xE4ECE0, 17);
    s_pages.link_text = label(pill, "51未连接", 8, 1, 148, &pet_font_22, 0x587461);
    lv_obj_set_style_text_align(s_pages.link_text, LV_TEXT_ALIGN_CENTER, 0);
    s_pages.catalog = box(page, 0, 154, 480, 566, BG, 0);
    card = box(s_pages.catalog, 32, 12, 416, 158, 0xE1EBDE, 26);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(card, detail_clicked, LV_EVENT_CLICKED, RT_NULL);
    for (i = 0; i < 4; i++)
        box(card, 25 + (i % 2) * 29, 28 + (i / 2) * 29, 23, 23,
            i == 1 ? 0xD9B475 : i == 2 ? 0xD79388 : GREEN, 7);
    label(card, "记忆挑战", 104, 24, 280, &pet_font_30, INK);
    label(card, "看灯光，记顺序", 104, 69, 280, &pet_font_22, MUTED);
    label(card, "查看玩法  >", 104, 110, 280, &pet_font_22, 0x587461);
    card = box(s_pages.catalog, 32, 190, 416, 142, PAPER, 26);
    label(card, "反应速度", 24, 24, 244, &pet_font_30, INK);
    label(card, "后续开放", 292, 28, 100, &pet_font_22, MUTED);
    label(card, "看准信号，挑战手速", 24, 80, 368, &pet_font_22, MUTED);
    card = box(s_pages.catalog, 32, 352, 416, 142, PAPER, 26);
    label(card, "体感挑战", 24, 24, 244, &pet_font_30, INK);
    label(card, "后续开放", 292, 28, 100, &pet_font_22, MUTED);
    label(card, "跟随提示，完成小动作", 24, 80, 368, &pet_font_22, MUTED);
    s_pages.catalog_hint = label(s_pages.catalog, "连接51后，即可开始记忆挑战", 32, 518, 416, &pet_font_22, MUTED);
    lv_obj_set_style_text_align(s_pages.catalog_hint, LV_TEXT_ALIGN_CENTER, 0);

    s_pages.detail = box(page, 0, 154, 480, 566, BG, 0);
    card = box(s_pages.detail, 32, 6, 416, 150, 0xE1EBDE, 26);
    create_mini(card);
    s_pages.mini_mood = label(card, "心情 30", 158, 24, 236, &pet_font_22, INK);
    s_pages.mini_trust = label(card, "亲密度 0.00", 158, 62, 236, &pet_font_22, INK);
    s_pages.mini_state = label(card, "待陪伴", 158, 101, 236, &pet_font_22, MUTED);
    card = box(s_pages.detail, 32, 172, 416, 246, PAPER, 26);
    s_pages.detail_title = label(card, "记住，再告诉我", 24, 20, 368, &pet_font_22, INK);
    s_pages.detail_body = label(card, "", 24, 68, 368, &pet_font_22, MUTED);
    s_pages.detail_hint = label(card, "", 24, 182, 368, &pet_font_22, 0x587461);
    s_pages.game_primary = button(s_pages.detail, "请连接51", 32, 434, 416, 56,
                                  GREEN, primary_clicked, RT_NULL, &s_pages.primary_text);
    s_pages.game_back = button(s_pages.detail, "返回游戏列表", 32, 502, 416, 46,
                               PAPER, back_clicked, RT_NULL, RT_NULL);
    show(s_pages.detail, 0);
}

static void create_status(lv_obj_t *page)
{
    static const char *names[] = {"心情", "精力", "亲密度"};
    static const char *rows[] = {"日常互动", "游戏次数", "胜利次数", "经验", "最佳反应"};
    lv_obj_t *card, *word;
    unsigned i;
    label(page, "我们的故事", 32, 26, 416, &pet_font_22, MUTED);
    label(page, "我的状态", 32, 62, 416, &pet_font_30, INK);
    s_pages.status_caption = label(page, "轻轻摇一摇，我会回应你", 32, 112, 416, &pet_font_22, MUTED);
    card = box(page, 32, 166, 416, 126, PAPER, 26);
    for (i = 0; i < 3; i++) {
        int x = i * 416 / 3;
        word = label(card, names[i], x, 18, 138, &pet_font_22, MUTED);
        lv_obj_set_style_text_align(word, LV_TEXT_ALIGN_CENTER, 0);
        s_pages.status_values[i] = label(card, "0", x, 59, 138, &pet_font_36, INK);
        lv_obj_set_style_text_align(s_pages.status_values[i], LV_TEXT_ALIGN_CENTER, 0);
        if (i) box(card, x, 22, 1, 82, 0xE7E9E0, 0);
    }
    card = box(page, 32, 310, 416, 276, PAPER, 26);
    for (i = 0; i < 5; i++) {
        int y = 20 + (int)i * 51;
        label(card, rows[i], 24, y, 192, &pet_font_22, MUTED);
        s_pages.status_rows[i] = label(card, "0", 212, y, 180, &pet_font_22, INK);
        lv_obj_set_style_text_align(s_pages.status_rows[i], LV_TEXT_ALIGN_RIGHT, 0);
        if (i < 4) box(card, 24, y + 45, 368, 1, 0xE7E9E0, 0);
    }
    s_pages.save_hint = label(page, "约30秒自动保存成长记录", 32, 592, 416, &pet_font_22, MUTED);
    lv_obj_set_style_text_align(s_pages.save_hint, LV_TEXT_ALIGN_CENTER, 0);
    s_pages.save_button = button(page, "保存成长记录", 32, 628, 416, 50, GREEN,
                                  save_clicked, RT_NULL, &s_pages.save_caption);
    word = label(page, "心情、精力重启后重新开始", 32, 685, 416, &pet_font_22, MUTED);
    lv_obj_set_style_text_align(word, LV_TEXT_ALIGN_CENTER, 0);
}

void pet_pages_create(void)
{
    static const char *names[] = {"陪伴", "游戏", "状态"};
    lv_obj_t *screen = lv_screen_active();
    unsigned i;
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    s_pages.root = box(screen, BSP_PET_PANEL_OFFSET_X, 0, BSP_PET_PANEL_WIDTH, 800, BG, 0);
    for (i = 0; i < 3; i++) s_pages.page[i] = box(s_pages.root, 0, 0, 480, 720, BG, 0);
    pet_ui_create(s_pages.page[PET_PAGE_HOME]);
    create_games(s_pages.page[PET_PAGE_GAMES]);
    create_status(s_pages.page[PET_PAGE_STATUS]);
    s_pages.service_status = label(s_pages.root, "AI:DEMO | 51:WAIT | AB1",
        170, 30, 278, &pet_font_22, MUTED);
    lv_obj_set_style_text_align(s_pages.service_status, LV_TEXT_ALIGN_RIGHT, 0);
    box(s_pages.root, 32, 721, 416, 1, 0xE1E5DB, 0);
    for (i = 0; i < 3; i++)
        s_pages.nav[i] = button(s_pages.root, names[i], 32 + (int)i * 142, 734, 132, 50,
                                PAPER, nav_clicked, (void *)(uintptr_t)i, &s_pages.nav_text[i]);
    pet_pages_show(PET_PAGE_HOME);
}

void pet_pages_show(u8 page)
{
    unsigned i;
    if (page > PET_PAGE_STATUS) return;
    if (page != s_pages.current) pet_ui_reset_input();
    s_pages.current = page;
    pet_ui_set_active(page == PET_PAGE_HOME);
    for (i = 0; i < 3; i++) show(s_pages.page[i], i == page);
    rt_kprintf("[A2] page=%u\n", page);
}

u8 pet_pages_current(void) { return s_pages.current; }

void pet_pages_reset(void)
{
    s_pages.locked = 0;
    back_clicked(RT_NULL);
    pet_pages_show(PET_PAGE_HOME);
}

void pet_pages_update(const PetAppView *view, u32 now)
{
    const PetGame *g = &view->game;
    const char *title, *body, *hint, *action;
    int active = pet_game_active(g), ready = g->connected && (g->capabilities & PET_GAME_CAP_SIMON);
    char value[64], progress[64], result[80];
    char service[PET_SERVICE_STATUS_TEXT_MAX];
    unsigned i;
    if (pet_service_status_format_current(service, sizeof(service)))
        set_text(s_pages.service_status, service);
    s_pages.locked = (u8)active;
    if (active && (!s_pages.detail_open || s_pages.current != PET_PAGE_GAMES)) {
        pet_pages_show(PET_PAGE_GAMES);
        detail_clicked(RT_NULL);
    }
    if (!s_pages.nav_drawn || s_pages.last_page != s_pages.current || s_pages.last_locked != active) {
        for (i = 0; i < 3; i++) {
            enable(s_pages.nav[i], !active || i == PET_PAGE_GAMES);
            lv_obj_set_style_bg_color(s_pages.nav[i], lv_color_hex(i == s_pages.current ? GREEN : PAPER), 0);
        }
        s_pages.nav_drawn = 1; s_pages.last_page = s_pages.current; s_pages.last_locked = (u8)active;
    }
    set_text(s_pages.link_text, g->demo ? "演示模式" : g->connected ? "51已连接" : "51未连接");
    set_text(s_pages.catalog_hint, g->demo ? "演示模式：未连接真实51" : ready ?
             "51已就绪，选记忆挑战开始吧" : g->connected ? "51尚未开放记忆挑战" : "连接51后，即可开始记忆挑战");
    if (s_pages.current == PET_PAGE_STATUS) {
        set_text(s_pages.status_caption, view->motion_ready ? "轻轻摇一摇，我会回应你" : "动作感应暂不可用，仍可触摸");
        rt_snprintf(value, sizeof(value), "%u", view->pet.mood); set_text(s_pages.status_values[0], value);
        rt_snprintf(value, sizeof(value), "%u", view->pet.energy); set_text(s_pages.status_values[1], value);
        rt_snprintf(value, sizeof(value), "%u.%02u", view->pet.trust_x100 / 100U, view->pet.trust_x100 % 100U);
        set_text(s_pages.status_values[2], value);
        rt_snprintf(value, sizeof(value), "%lu", (unsigned long)view->interactions); set_text(s_pages.status_rows[0], value);
        rt_snprintf(value, sizeof(value), "%lu", (unsigned long)view->games); set_text(s_pages.status_rows[1], value);
        rt_snprintf(value, sizeof(value), "%lu", (unsigned long)view->wins); set_text(s_pages.status_rows[2], value);
        rt_snprintf(value, sizeof(value), "%lu", (unsigned long)view->pet.exp); set_text(s_pages.status_rows[3], value);
        if (view->best_reaction_ms) rt_snprintf(value, sizeof(value), "%lu ms", (unsigned long)view->best_reaction_ms);
        else rt_snprintf(value, sizeof(value), "暂无记录");
        set_text(s_pages.status_rows[4], value);
        hint = view->diagnostic || g->demo ? "测试中的变化不会保存" :
               !view->store.ready ? "存档暂不可用，成长尚未保存" :
               view->store.saving || view->store.pending ? "正在保存我们的成长记录" :
               view->store.error ? "保存未成功，请点击重试" :
               !view->store.generation && !view->store.writes && !view->store.loaded ? "约30秒自动保存成长记录" :
               view->growth_pending ? "有新记录，稍后自动保存" :
               view->store.loaded && !view->store.writes ? "已找回上次保存的成长记录" : "成长记录已保存";
        set_text(s_pages.save_hint, hint);
        set_text(s_pages.save_caption, view->store.pending || view->store.saving ? "正在保存" :
                 view->store.error && view->store.ready ? "重试保存" : "保存成长记录");
        enable(s_pages.save_button, view->store.ready && !view->store.pending && !view->store.saving &&
                                      !view->diagnostic && !g->demo);
    }
    if (s_pages.current != PET_PAGE_GAMES || !s_pages.detail_open) return;
    rt_snprintf(value, sizeof(value), "心情 %u", view->pet.mood); set_text(s_pages.mini_mood, value);
    rt_snprintf(value, sizeof(value), "亲密度 %u.%02u", view->pet.trust_x100 / 100U, view->pet.trust_x100 % 100U);
    set_text(s_pages.mini_trust, value); set_text(s_pages.mini_state, emotion(view->pet.display));
    for (i = 0; i < 2; i++) {
        int dizzy = view->pet.display == PET_DIZZY;
        int closed = view->pet.display == PET_HAPPY || view->pet.display == PET_EXCITED || (now % 4500UL) >= 4340UL;
        show(s_pages.mini_eyes[i], !closed && !dizzy); show(s_pages.mini_closed[i], closed && !dizzy);
        show(s_pages.mini_dizzy[i], dizzy);
    }
    title = "记住，再告诉我";
    body = "看51灯光，记住亮起顺序\n用51按键，重复刚才的顺序\n一共4轮，序列会逐轮变长";
    hint = ready ? "准备好后，点击开始游戏" : "连接51后即可开始游玩";
    action = ready ? "开始游戏" : "请连接51";
    if (g->connected && !ready) { hint = "这块51还未开放记忆挑战"; action = "暂未开放"; }
    if (g->phase == PET_GAME_STARTING) {
        title = "正在准备"; body = "正在等待51确认开始\n准备好后会进入第一轮";
        hint = "等待期间也可以取消"; action = "取消开始";
    } else if (g->phase == PET_GAME_PLAYING) {
        rt_snprintf(progress, sizeof(progress), "第 %u / %u 轮", g->round, g->total);
        title = progress;
        body = g->step == PET_GAME_INPUT ? "轮到你啦\n请在51上按顺序输入" : "请看51灯光\n记住这一轮的亮起顺序";
        hint = "小团子会陪你完成这局"; action = "退出游戏";
    } else if (g->phase == PET_GAME_CANCELLING) {
        title = "正在结束本局"; body = "等待51停止游戏\n结束后就能返回陪伴页面";
        if (g->error == PET_GAME_MOTION_INTERRUPTED) {
            title = "先让我缓一缓"; body = "摇得有点急，我头晕啦\n正在结束这局游戏";
        }
        hint = "不会结算这一局的奖励"; action = "正在退出";
    } else if (g->phase == PET_GAME_FINISHED) {
        title = g->result == GAME_WIN ? "挑战成功" : g->result == GAME_FAIL ? "下次再试试" :
                g->result == GAME_TIMEOUT ? "这局等待超时了" : "本局已结束";
        rt_snprintf(result, sizeof(result), "完成 %lu / 4 轮\n%s", (unsigned long)g->score,
                     g->result == GAME_WIN ? "记得真棒，和你玩很开心" :
                     g->result == GAME_CANCEL ? "想玩的时候，再来找我吧" : "没关系，我们再试一次吧");
        body = result;
        hint = g->demo ? "演示结束后会恢复原来数值" : "心情与记录已按本局结果更新";
        action = ready ? "再玩一次" : "请连接51";
    } else if (g->phase == PET_GAME_ERROR) {
        title = "这次没有开始或已中断";
        body = g->error == PET_GAME_NO_LINK || g->error == PET_GAME_LINK_LOST ? "51连接已断开\n重新连接后可以再玩" :
               g->error == PET_GAME_UNSUPPORTED ? "这块51还未开放记忆挑战" :
               g->error == PET_GAME_BUSY ? "小团子暂时不适合玩游戏\n稍后再来试试吧" :
               g->error == PET_GAME_REJECTED ? "51暂时不能开始游戏\n请稍后重试" :
               g->error == PET_GAME_MOTION_INTERRUPTED ? "刚才摇得有点急\n把我放稳，休息一会儿吧" :
               "等待回应超时\n请确认51连接后再试";
        hint = "这次中断不会增加游戏奖励";
    }
    set_text(s_pages.detail_title, title); set_text(s_pages.detail_body, body);
    set_text(s_pages.detail_hint, hint); set_text(s_pages.primary_text, action);
    enable(s_pages.game_primary, g->phase == PET_GAME_STARTING || g->phase == PET_GAME_PLAYING ||
                                  (!active && ready));
    enable(s_pages.game_back, !active);
}

void pet_pages_log_layout(void)
{
    lv_area_t nav;
    lv_obj_get_coords(s_pages.nav[PET_PAGE_GAMES], &nav);
    rt_kprintf("A2_UI page=%u detail=%u locked=%u nav_games=%ld,%ld,%ld,%ld\n",
               s_pages.current, s_pages.detail_open, s_pages.locked,
               (long)nav.x1, (long)nav.y1, (long)nav.x2, (long)nav.y2);
}
