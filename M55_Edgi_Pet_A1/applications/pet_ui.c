#include "pet_ui.h"
#include "pet_app.h"
#include "pet_logic/edgi/interaction_hold.h"
#include <lvgl.h>

LV_FONT_DECLARE(pet_font_22);
LV_FONT_DECLARE(pet_font_30);
LV_FONT_DECLARE(pet_font_36);

#define C_INK    0x354A41
#define C_MUTED  0x738278
#define C_BG     0xF7F5EC
#define C_GREEN  0x83A992
#define C_PET    0xFFFBED
#define C_BLUSH  0xEFC2AA

typedef struct {
    lv_obj_t *root, *head, *body_hit, *message, *state, *hold_bar;
    lv_obj_t *eyes[2], *closed[2], *shine[2], *hearts[2];
    lv_obj_t *dizzy_eyes[2], *stars[3], *mouth, *wavy;
    lv_obj_t *arms[2], *hug_heart;
    lv_obj_t *value[3], *bars[3];
    u8 mood, energy, display, feedback, blink, progress, dizzy_phase;
    u16 trust;
    u32 hug_at;
    int arm_inset;
    PetHold hold;
} PetView;
static PetView s_view;
static lv_indev_t *s_test_indev;
static lv_point_t s_test_point;
static u8 s_test_pressed;
static u32 s_test_started;
static u8 s_active = 1;

static rt_bool_t is_physical_event(lv_event_t *event)
{
    return lv_event_get_indev(event) != s_test_indev;
}

static void test_pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    /* A forgotten diagnostic release cannot leave the UI held indefinitely. */
    if (s_test_pressed && (u32)(lv_tick_get() - s_test_started) >= 10000UL)
        s_test_pressed = 0;
    data->point = s_test_point;
    data->state = s_test_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int pet_ui_test_touch(u8 action, u16 x, u16 y)
{
    if (action > PET_TEST_TOUCH_MOVE || x >= 480 || y >= 800) return -RT_EINVAL;
    if (!s_test_indev) {
        s_test_indev = lv_indev_create();
        if (!s_test_indev) return -RT_ENOMEM;
        lv_indev_set_type(s_test_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_test_indev, test_pointer_read);
    }
    if (action == PET_TEST_TOUCH_DOWN) {
        if (s_test_pressed) return -RT_EBUSY;
        s_test_started = lv_tick_get();
        s_test_pressed = 1;
    } else if (action == PET_TEST_TOUCH_MOVE && !s_test_pressed) return -RT_EINVAL;
    else if (action == PET_TEST_TOUCH_UP) s_test_pressed = 0;
    if (action != PET_TEST_TOUCH_UP) {
        s_test_point.x = x + BSP_PET_PANEL_OFFSET_X;
        s_test_point.y = y;
    }
    return RT_EOK;
}

void pet_ui_reset_input(void)
{
    pet_hold_cancel(&s_view.hold);
    s_test_pressed = 0;
    if (s_test_indev) lv_indev_reset(s_test_indev, NULL);
}

void pet_ui_set_active(u8 active)
{
    s_active = active;
    if (!active) pet_ui_reset_input();
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                     uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *text_at(lv_obj_t *parent, const char *text, int x, int y,
                        int w, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    if (w > 0) lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static void center_text(lv_obj_t *label)
{
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static lv_obj_t *line(lv_obj_t *parent, int x, int y,
                      const lv_point_precise_t *points, uint32_t count, int width)
{
    lv_obj_t *obj = lv_line_create(parent);
    lv_line_set_points(obj, points, count);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_line_color(obj, lv_color_hex(C_INK), 0);
    lv_obj_set_style_line_width(obj, width, 0);
    lv_obj_set_style_line_rounded(obj, true, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *heart(lv_obj_t *parent, int x, int y, int size)
{
    lv_obj_t *group = box(parent, x, y, size, size, C_BG, 0);
    int half = size / 2;
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
    box(group, 0, 0, half + 2, half + 2, 0xDC8D83, LV_RADIUS_CIRCLE);
    box(group, half - 2, 0, half + 2, half + 2, 0xDC8D83, LV_RADIUS_CIRCLE);
    /* The lower diamond and two circles form a compact vector heart. */
    {
        lv_obj_t *diamond = box(group, half / 2, half / 2 - 1,
                                 half + 3, half + 3, 0xDC8D83, 2);
        lv_obj_set_style_transform_pivot_x(diamond, (half + 3) / 2, 0);
        lv_obj_set_style_transform_pivot_y(diamond, (half + 3) / 2, 0);
        lv_obj_set_style_transform_rotation(diamond, 450, 0);
    }
    return group;
}

static void head_clicked(lv_event_t *event)
{
    if (!s_active) return;
    /* CLICKED is emitted once on release, including a long press; PRESSING
     * and LONG_PRESSED_REPEAT are deliberately not interpreted as taps. */
    if (pet_app_post_head(is_physical_event(event)) != RT_EOK)
        rt_kprintf("[A1] touch event queue full; input not applied\n");
}

static void body_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    u32 now = lv_tick_get();
    if (!s_active) { pet_hold_cancel(&s_view.hold); return; }
    if (code == LV_EVENT_PRESSED) pet_hold_begin(&s_view.hold, now);
    else if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
        if (pet_hold_update(&s_view.hold, now) &&
            pet_app_post_hug(is_physical_event(event)) != RT_EOK)
            rt_kprintf("[A1] hug event queue full; input not applied\n");
        if (code == LV_EVENT_RELEASED) pet_hold_cancel(&s_view.hold);
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET)
        pet_hold_cancel(&s_view.hold);
}

static void create_character(lv_obj_t *stage)
{
    static const lv_point_precise_t smile[] = {{0, 0}, {7, 7}, {14, 9}, {21, 7}, {28, 0}};
    static const lv_point_precise_t closed[] = {{0, 9}, {9, 1}, {18, 9}};
    static const lv_point_precise_t cross[] = {{0,0},{20,24},{10,12},{20,0},{0,24}};
    static const lv_point_precise_t wave[] = {{0,6},{7,0},{14,6},{21,0},{28,6}};
    lv_obj_t *head;
    unsigned i;

    box(stage, 103, 304, 210, 20, 0xCCDCCE, LV_RADIUS_CIRCLE);
    box(stage, 128, 203, 160, 106, C_PET, 48);
    box(stage, 110, 286, 68, 39, C_PET, 20);
    box(stage, 238, 286, 68, 39, C_PET, 20);

    head = box(stage, 69, 26, 278, 218, C_PET, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    s_view.head = head;
    box(head, 12, 0, 78, 105, C_PET, 39);
    box(head, 188, 0, 78, 105, C_PET, 39);
    box(head, 30, 16, 42, 67, C_BLUSH, 22);
    box(head, 206, 16, 42, 67, C_BLUSH, 22);
    box(head, 0, 48, 278, 169, C_PET, 83);
    box(head, 44, 151, 37, 17, C_BLUSH, LV_RADIUS_CIRCLE);
    box(head, 197, 151, 37, 17, C_BLUSH, LV_RADIUS_CIRCLE);

    for (i = 0; i < 2; i++) {
        int x = i == 0 ? 78 : 178;
        s_view.eyes[i] = box(head, x, 111, 23, 37, C_INK, 12);
        s_view.shine[i] = box(s_view.eyes[i], 5, 5, 7, 8, 0xFFFFFF, 4);
        s_view.closed[i] = line(head, x + 2, 127, closed, 3, 6);
        lv_obj_add_flag(s_view.closed[i], LV_OBJ_FLAG_HIDDEN);
        s_view.dizzy_eyes[i] = line(head, x + 2, 115, cross, 5, 4);
        lv_obj_add_flag(s_view.dizzy_eyes[i], LV_OBJ_FLAG_HIDDEN);
    }
    box(head, 132, 145, 14, 9, 0xA78068, 5);
    s_view.mouth = line(head, 125, 163, smile, 5, 4);
    s_view.wavy = line(head, 125, 166, wave, 5, 4);
    lv_obj_add_flag(s_view.wavy, LV_OBJ_FLAG_HIDDEN);
    for (i = 0; i < 3; i++) {
        lv_obj_t *star = box(stage, i == 0 ? 32 : i == 1 ? 357 : 306,
                              i == 0 ? 102 : i == 1 ? 130 : 23, 22, 22, C_BG, 0);
        lv_obj_set_style_bg_opa(star, LV_OPA_TRANSP, 0);
        box(star, 8, 0, 6, 22, 0xD9B475, 3);
        box(star, 0, 8, 22, 6, 0xD9B475, 3);
        lv_obj_set_style_transform_pivot_x(star, 11, 0);
        lv_obj_set_style_transform_pivot_y(star, 11, 0);
        s_view.stars[i] = star;
        lv_obj_add_flag(star, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_add_flag(head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(head, head_clicked, LV_EVENT_CLICKED, RT_NULL);
    s_view.hug_heart = heart(stage, 184, 247, 48);
    lv_obj_add_flag(s_view.hug_heart, LV_OBJ_FLAG_HIDDEN);
    for (i = 0; i < 2; i++) {
        s_view.arms[i] = box(stage, i == 0 ? 108 : 248, 254, 60, 31, C_PET, 16);
        lv_obj_set_style_border_color(s_view.arms[i], lv_color_hex(C_BLUSH), 0);
        lv_obj_set_style_border_width(s_view.arms[i], 2, 0);
    }
    /* The body target sits below the head. Each press belongs to one target;
     * leaving it cancels the gesture until a fresh physical press. */
    s_view.body_hit = box(stage, 104, 245, 208, 80, C_PET, 0);
    lv_obj_set_style_bg_opa(s_view.body_hit, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_view.body_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_view.body_hit, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_view.body_hit, body_event, LV_EVENT_ALL, RT_NULL);
    s_view.hearts[0] = heart(stage, 341, 44, 31);
    s_view.hearts[1] = heart(stage, 35, 89, 22);
    lv_obj_add_flag(s_view.hearts[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_view.hearts[1], LV_OBJ_FLAG_HIDDEN);
}

void pet_ui_create(lv_obj_t *parent)
{
    lv_obj_t *root, *pill, *stage, *stats, *caption;
    static const char *names[] = {"心情", "精力", "亲密度"};
    static const uint32_t colors[] = {0x8BAF94, 0xD9B475, 0xD79388};
    unsigned i;

    /* The page shell owns the calibrated x=32 screen origin. */
    root = box(parent, 0, 0, BSP_PET_PANEL_WIDTH, 800, C_BG, 0);
    s_view.root = root;
    text_at(root, "电子宠物", 32, 26, 200, &pet_font_22, C_MUTED);
    text_at(root, "你好，小团子", 32, 62, 310, &pet_font_30, C_INK);
    text_at(root, "摸摸头、抱抱，或轻轻摇一摇", 32, 112, 416, &pet_font_22, C_MUTED);
    pill = box(root, 335, 35, 113, 36, 0xE4ECE0, 18);
    box(pill, 12, 15, 7, 7, 0x6C9A7C, LV_RADIUS_CIRCLE);
    text_at(pill, "陪伴中", 29, 2, 80, &pet_font_22, 0x587461);

    stage = box(root, 32, 154, 416, 376, 0xE1EBDE, 30);
    box(stage, 24, 22, 34, 5, 0xC9DCC8, 3);
    box(stage, 39, 8, 5, 34, 0xC9DCC8, 3);
    box(stage, 352, 282, 23, 4, 0xC9DCC8, 2);
    box(stage, 362, 272, 4, 23, 0xC9DCC8, 2);
    create_character(stage);
    s_view.hold_bar = lv_bar_create(stage);
    lv_obj_set_pos(s_view.hold_bar, 120, 328);
    lv_obj_set_size(s_view.hold_bar, 176, 5);
    lv_bar_set_range(s_view.hold_bar, 0, 100);
    lv_obj_set_style_bg_color(s_view.hold_bar, lv_color_hex(0xCCDCCE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.hold_bar, lv_color_hex(0xD79388), LV_PART_INDICATOR);
    lv_obj_remove_flag(s_view.hold_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_view.hold_bar, LV_OBJ_FLAG_HIDDEN);
    s_view.state = text_at(stage, "自在", 16, 340, 384, &pet_font_22, 0x66826C);
    center_text(s_view.state);

    s_view.message = text_at(root, "点点我的头，打个招呼吧", 32, 545, 416, &pet_font_22, C_INK);
    center_text(s_view.message);
    stats = box(root, 32, 589, 416, 120, 0xFFFDF6, 24);
    lv_obj_set_style_border_color(stats, lv_color_hex(0xE6E6DC), 0);
    lv_obj_set_style_border_width(stats, 1, 0);
    box(stats, 138, 23, 1, 83, 0xE7E9E0, 0);
    box(stats, 277, 23, 1, 83, 0xE7E9E0, 0);
    for (i = 0; i < 3; i++) {
        int x = (int)i * 416 / 3;
        caption = text_at(stats, names[i], x, 13, 138, &pet_font_22, C_MUTED);
        center_text(caption);
        s_view.value[i] = text_at(stats, "0", x, 47, 138, &pet_font_36, C_INK);
        center_text(s_view.value[i]);
        s_view.bars[i] = lv_bar_create(stats);
        lv_obj_set_pos(s_view.bars[i], x + 26, 103);
        lv_obj_set_size(s_view.bars[i], 86, 5);
        lv_obj_set_style_bg_color(s_view.bars[i], lv_color_hex(0xECEDE4), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_view.bars[i], lv_color_hex(colors[i]), LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_view.bars[i], 3, LV_PART_MAIN);
        lv_obj_set_style_radius(s_view.bars[i], 3, LV_PART_INDICATOR);
        lv_bar_set_range(s_view.bars[i], 0, i == 2 ? 10000 : 100);
        lv_obj_remove_flag(s_view.bars[i], LV_OBJ_FLAG_CLICKABLE);
    }
    s_view.mood = s_view.energy = s_view.display = s_view.feedback = s_view.blink = 255;
    s_view.trust = 65535;
    s_view.progress = 255;
    s_view.dizzy_phase = 255;
    s_view.arm_inset = -1;
}

void pet_ui_log_layout(void)
{
    lv_area_t head, body, root;
    lv_obj_update_layout(s_view.root);
    lv_obj_get_coords(s_view.root, &root);
    lv_obj_get_coords(s_view.head, &head);
    lv_obj_get_coords(s_view.body_hit, &body);
    rt_kprintf("A1_LAYOUT width=%ld height=%ld render_width=%ld offset_x=%ld root=%ld,%ld,%ld,%ld head=%ld,%ld,%ld,%ld body=%ld,%ld,%ld,%ld hold_ms=%lu\n",
               (long)BSP_PET_PANEL_WIDTH,
               (long)lv_display_get_vertical_resolution(NULL),
               (long)lv_display_get_horizontal_resolution(NULL),
               (long)BSP_PET_PANEL_OFFSET_X,
               (long)root.x1, (long)root.y1, (long)root.x2, (long)root.y2,
               (long)head.x1, (long)head.y1, (long)head.x2, (long)head.y2,
               (long)body.x1, (long)body.y1, (long)body.x2, (long)body.y2,
               (unsigned long)PET_HUG_HOLD_MS);
}

static void visible(lv_obj_t *obj, int show)
{
    if (show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void pet_ui_update(const PetSnapshot *snapshot, u8 feedback, u32 now)
{
    int happy = snapshot->display == PET_HAPPY || snapshot->display == PET_EXCITED;
    int dizzy = snapshot->display == PET_DIZZY;
    int hugging = happy && feedback == PET_FEEDBACK_HUG;
    u8 blink = !happy && !dizzy && (now % 4500UL) >= 4340UL;
    u8 dizzy_phase = (u8)((now / 180UL) % 4UL);
    u8 progress = s_view.hold.active ? pet_hold_progress(&s_view.hold, now) : 255;
    int inset = 0;
    unsigned i;
    if (dizzy && s_active && s_view.dizzy_phase != dizzy_phase) {
        for (i = 0; i < 3; i++) {
            int base = i == 0 ? 102 : i == 1 ? 130 : 23;
            lv_obj_set_y(s_view.stars[i], base + ((dizzy_phase + i) % 4 < 2 ? -3 : 3));
            lv_obj_set_style_transform_rotation(s_view.stars[i], (dizzy_phase + i) * 150, 0);
        }
        s_view.dizzy_phase = dizzy_phase;
    }
    if (hugging && s_view.feedback != PET_FEEDBACK_HUG) s_view.hug_at = now;
    if (hugging) {
        u32 elapsed = (u32)(now - s_view.hug_at);
        inset = elapsed < 250UL ? (int)(elapsed * 46UL / 250UL) : 46;
    }
    if (s_view.arm_inset != inset) {
        lv_obj_set_x(s_view.arms[0], 108 + inset);
        lv_obj_set_x(s_view.arms[1], 248 - inset);
        s_view.arm_inset = inset;
    }
    if (s_view.mood != snapshot->mood) {
        lv_label_set_text_fmt(s_view.value[0], "%u", (unsigned)snapshot->mood);
        lv_bar_set_value(s_view.bars[0], snapshot->mood, LV_ANIM_OFF);
        s_view.mood = snapshot->mood;
    }
    if (s_view.energy != snapshot->energy) {
        lv_label_set_text_fmt(s_view.value[1], "%u", (unsigned)snapshot->energy);
        lv_bar_set_value(s_view.bars[1], snapshot->energy, LV_ANIM_OFF);
        s_view.energy = snapshot->energy;
    }
    if (s_view.trust != snapshot->trust_x100) {
        lv_label_set_text_fmt(s_view.value[2], "%u.%02u", snapshot->trust_x100 / 100U,
                              snapshot->trust_x100 % 100U);
        lv_bar_set_value(s_view.bars[2], snapshot->trust_x100, LV_ANIM_OFF);
        s_view.trust = snapshot->trust_x100;
    }
    if (s_view.display != snapshot->display || s_view.blink != blink || s_view.feedback != feedback) {
        for (i = 0; i < 2; i++) {
            visible(s_view.eyes[i], !happy && !blink && !dizzy);
            visible(s_view.closed[i], happy || blink);
            visible(s_view.dizzy_eyes[i], dizzy);
        }
        visible(s_view.mouth, !dizzy); visible(s_view.wavy, dizzy);
        for (i = 0; i < 3; i++) visible(s_view.stars[i], dizzy);
        lv_label_set_text(s_view.state, dizzy ? "晕乎乎" : hugging ? "抱抱中" : happy ? "开心" :
                          snapshot->display == PET_SAD ? "待陪伴" : "自在");
        s_view.blink = blink;
    }
    if (s_view.feedback != feedback || s_view.display != snapshot->display || s_view.progress != progress) {
        const char *message = dizzy ? "有点晕，先把我放稳吧" : hugging ? "抱抱你，心里暖暖的" :
                              feedback == PET_FEEDBACK_LIGHT ? "轻轻摇，像坐小摇篮一样" :
                              feedback == PET_FEEDBACK_HEAD ? "摸摸真舒服，谢谢你" :
                              feedback == PET_FEEDBACK_COOLDOWN ? "我收到啦，陪我待一会儿" :
                              progress == 100 ? "松开手，再给我一个抱抱" :
                              progress != 255 ? "再靠近一点，抱抱准备中" :
                              happy ? "和你在一起真开心" : "点点我的头，打个招呼吧";
        lv_label_set_text(s_view.message, message);
        visible(s_view.hearts[0], !dizzy && (feedback == PET_FEEDBACK_HEAD || feedback == PET_FEEDBACK_LIGHT || hugging));
        visible(s_view.hearts[1], !dizzy && (feedback == PET_FEEDBACK_HEAD || feedback == PET_FEEDBACK_LIGHT || hugging));
        visible(s_view.hug_heart, hugging);
        visible(s_view.hold_bar, progress != 255);
        if (progress != 255) lv_bar_set_value(s_view.hold_bar, progress, LV_ANIM_OFF);
        s_view.progress = progress;
        s_view.feedback = feedback;
    }
    s_view.display = snapshot->display;
}
