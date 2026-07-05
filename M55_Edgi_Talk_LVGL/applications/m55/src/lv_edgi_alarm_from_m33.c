/**
 * 轮询 M33 写入的共享内存（0x261C0000，m33_m55_shared），在 LVGL 上显示类别与 seq。
 * 头文件使用相对路径，便于 RT-Thread Studio（Eclipse makefile）不额外配置 -I 也能编译。
 */
#include "../include/edgi_m33_m55_shm.h"
#include "../include/edgi_shm_cache.h"
#include "../include/lv_edgi_alarm_from_m33.h"
#include "lvgl.h"
#include <rtthread.h>

LV_FONT_DECLARE(edgi_font_ui_cn_16);

#include <stdint.h>
#include <stdio.h>

static lv_obj_t *s_scr_root;
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_detail;
static lv_obj_t *s_lbl_history;
static uint32_t  s_last_shown_seq;
static uint8_t   s_last_shown_type = 0xffu;
static uint32_t  s_shm_read_fail;
static uint32_t  s_shm_poll_ms = 250u;
static uint8_t   s_timer_beat_div;

typedef struct
{
    uint8_t wifi;
    uint8_t upload;
    uint16_t heartbeat;
} edgi_m55_runtime_state_t;

static edgi_m55_runtime_state_t edgi_m55_state_cached(void)
{
    volatile edgi_m33_m55_shm_t *p = edgi_m33_m55_shm_get();
    edgi_m55_runtime_state_t st;

    edgi_shm_invalidate_m33_cmd((void *)p);
    st.wifi = p->m55_wifi_state;
    st.upload = p->m55_upload_state;
    st.heartbeat = p->m55_heartbeat;
    return st;
}

#ifndef EDGI_LV_ALARM_HISTORY_LINES
#define EDGI_LV_ALARM_HISTORY_LINES 6
#endif
#ifndef EDGI_LV_ALARM_HISTORY_VISIBLE_LINES
#define EDGI_LV_ALARM_HISTORY_VISIBLE_LINES 2
#endif
static char      s_hist_ring[EDGI_LV_ALARM_HISTORY_LINES][56];
static uint8_t   s_hist_next;
static uint8_t   s_hist_count;
static lv_timer_t *s_title_flash_timer;
static lv_obj_t *s_status_cards[4];
static lv_obj_t *s_status_icons[4];
static lv_obj_t *s_status_texts[4];

static lv_style_t s_style_bg;
static lv_style_t s_style_bg_lp;
static lv_style_t s_style_card_normal;
static lv_style_t s_style_card_lp;
static lv_style_t s_style_card_alert;
static lv_style_t s_style_card_knock;
static lv_style_t s_style_card_baby;
static lv_style_t s_style_icon_normal;
static lv_style_t s_style_icon_lp;
static lv_style_t s_style_icon_alert;
static lv_style_t s_style_icon_knock;
static lv_style_t s_style_icon_baby;
static lv_style_t s_style_title_lp;
static uint8_t s_style_ready;

/* 四宫格：左上起与 M33 alarm_type 映射 —— 0=环境/低功耗(不高亮格)，1 火 2 敲 3 婴 4 水 */
enum
{
    EDGI_SLOT_KNOCK = 0,
    EDGI_SLOT_FIRE,
    EDGI_SLOT_BABY,
    EDGI_SLOT_WATER,
    EDGI_SLOT_COUNT
};

#define EDGI_SLOT_NONE 0xFFu

/* alarm_type -> 卡片槽位；0 或未知 -> 低功耗，无高亮槽 */
static uint8_t edgi_alarm_type_to_slot(uint8_t alarm_type)
{
    switch (alarm_type)
    {
    case 1u:
        return EDGI_SLOT_FIRE;
    case 2u:
        return EDGI_SLOT_KNOCK;
    case 3u:
        return EDGI_SLOT_BABY;
    case 4u:
        return EDGI_SLOT_WATER;
    case 0u:
    default:
        return EDGI_SLOT_NONE;
    }
}

static void edgi_strip_card_highlight(uint8_t i)
{
    lv_obj_remove_style(s_status_cards[i], &s_style_card_alert, 0);
    lv_obj_remove_style(s_status_cards[i], &s_style_card_knock, 0);
    lv_obj_remove_style(s_status_cards[i], &s_style_card_baby, 0);
    lv_obj_remove_style(s_status_icons[i], &s_style_icon_alert, 0);
    lv_obj_remove_style(s_status_icons[i], &s_style_icon_knock, 0);
    lv_obj_remove_style(s_status_icons[i], &s_style_icon_baby, 0);
}

static void edgi_set_low_power_visual(uint8_t on)
{
    uint8_t i;
    if (on)
    {
        lv_obj_remove_style(s_scr_root, &s_style_bg, 0);
        lv_obj_add_style(s_scr_root, &s_style_bg_lp, 0);
        lv_obj_add_style(s_lbl_title, &s_style_title_lp, 0);
        for (i = 0; i < EDGI_SLOT_COUNT; i++)
        {
            edgi_strip_card_highlight(i);
            lv_obj_remove_style(s_status_cards[i], &s_style_card_normal, 0);
            lv_obj_add_style(s_status_cards[i], &s_style_card_lp, 0);
            lv_obj_remove_style(s_status_icons[i], &s_style_icon_normal, 0);
            lv_obj_add_style(s_status_icons[i], &s_style_icon_lp, 0);
            lv_obj_set_style_text_opa(s_status_texts[i], LV_OPA_60, 0);
        }
    }
    else
    {
        lv_obj_remove_style(s_scr_root, &s_style_bg_lp, 0);
        lv_obj_add_style(s_scr_root, &s_style_bg, 0);
        lv_obj_remove_style(s_lbl_title, &s_style_title_lp, 0);
        lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0x000000), 0);
        for (i = 0; i < EDGI_SLOT_COUNT; i++)
        {
            lv_obj_remove_style(s_status_cards[i], &s_style_card_lp, 0);
            lv_obj_add_style(s_status_cards[i], &s_style_card_normal, 0);
            lv_obj_remove_style(s_status_icons[i], &s_style_icon_lp, 0);
            lv_obj_add_style(s_status_icons[i], &s_style_icon_normal, 0);
            lv_obj_set_style_text_opa(s_status_texts[i], LV_OPA_COVER, 0);
        }
    }
}

static void edgi_set_status_active(uint8_t slot_idx)
{
    uint8_t i;
    edgi_set_low_power_visual(0u);
    for (i = 0; i < EDGI_SLOT_COUNT; i++)
        edgi_strip_card_highlight(i);

    if (slot_idx >= EDGI_SLOT_COUNT)
        return;

    i = slot_idx;
    if (i == EDGI_SLOT_KNOCK)
    {
        lv_obj_add_style(s_status_cards[i], &s_style_card_knock, 0);
        lv_obj_add_style(s_status_icons[i], &s_style_icon_knock, 0);
    }
    else if (i == EDGI_SLOT_BABY)
    {
        lv_obj_add_style(s_status_cards[i], &s_style_card_baby, 0);
        lv_obj_add_style(s_status_icons[i], &s_style_icon_baby, 0);
    }
    else
    {
        lv_obj_add_style(s_status_cards[i], &s_style_card_alert, 0);
        lv_obj_add_style(s_status_icons[i], &s_style_icon_alert, 0);
    }
}

static void edgi_apply_alarm_visual(uint8_t alarm_type)
{
    uint8_t slot = edgi_alarm_type_to_slot(alarm_type);
    if (slot == EDGI_SLOT_NONE)
    {
        uint8_t i;
        edgi_set_low_power_visual(1u);
        for (i = 0; i < EDGI_SLOT_COUNT; i++)
            edgi_strip_card_highlight(i);
        return;
    }
    edgi_set_status_active(slot);
}

static void edgi_ui_prepare_styles(void)
{
    if (s_style_ready)
        return;

    lv_style_init(&s_style_bg);
    lv_style_set_bg_color(&s_style_bg, lv_color_hex(0xEEF5FF));
    lv_style_set_bg_opa(&s_style_bg, LV_OPA_100);

    lv_style_init(&s_style_bg_lp);
    lv_style_set_bg_color(&s_style_bg_lp, lv_color_hex(0xB8C4D4));
    lv_style_set_bg_opa(&s_style_bg_lp, LV_OPA_100);

    lv_style_init(&s_style_card_normal);
    lv_style_set_bg_color(&s_style_card_normal, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&s_style_card_normal, LV_OPA_100);
    lv_style_set_border_color(&s_style_card_normal, lv_color_hex(0xC9D6E8));
    lv_style_set_border_width(&s_style_card_normal, 2);
    lv_style_set_radius(&s_style_card_normal, 12);
    lv_style_set_pad_all(&s_style_card_normal, 8);

    lv_style_init(&s_style_card_lp);
    lv_style_set_bg_color(&s_style_card_lp, lv_color_hex(0xD5DDE8));
    lv_style_set_bg_opa(&s_style_card_lp, LV_OPA_COVER);
    lv_style_set_border_color(&s_style_card_lp, lv_color_hex(0xA8B4C4));
    lv_style_set_border_width(&s_style_card_lp, 1);
    lv_style_set_radius(&s_style_card_lp, 12);
    lv_style_set_pad_all(&s_style_card_lp, 8);

    lv_style_init(&s_style_card_alert);
    lv_style_set_bg_color(&s_style_card_alert, lv_color_hex(0xFFE7E7));
    lv_style_set_border_color(&s_style_card_alert, lv_color_hex(0xD22A2A));
    lv_style_set_border_width(&s_style_card_alert, 3);

    lv_style_init(&s_style_card_knock);
    lv_style_set_bg_color(&s_style_card_knock, lv_color_hex(0xFFF3E0));
    lv_style_set_border_color(&s_style_card_knock, lv_color_hex(0xE67E22));
    lv_style_set_border_width(&s_style_card_knock, 3);

    lv_style_init(&s_style_card_baby);
    lv_style_set_bg_color(&s_style_card_baby, lv_color_hex(0xEAF3FF));
    lv_style_set_border_color(&s_style_card_baby, lv_color_hex(0x2A78D2));
    lv_style_set_border_width(&s_style_card_baby, 3);

    lv_style_init(&s_style_icon_normal);
    lv_style_set_text_color(&s_style_icon_normal, lv_color_hex(0x34495E));
    lv_style_set_text_font(&s_style_icon_normal, &lv_font_montserrat_24);

    lv_style_init(&s_style_icon_lp);
    lv_style_set_text_color(&s_style_icon_lp, lv_color_hex(0x7A8799));
    lv_style_set_text_font(&s_style_icon_lp, &lv_font_montserrat_24);

    lv_style_init(&s_style_icon_alert);
    lv_style_set_text_color(&s_style_icon_alert, lv_color_hex(0xD22A2A));

    lv_style_init(&s_style_icon_knock);
    lv_style_set_text_color(&s_style_icon_knock, lv_color_hex(0xE67E22));

    lv_style_init(&s_style_icon_baby);
    lv_style_set_text_color(&s_style_icon_baby, lv_color_hex(0x2A78D2));

    lv_style_init(&s_style_title_lp);
    lv_style_set_text_color(&s_style_title_lp, lv_color_hex(0x5A6570));

    s_style_ready = 1u;
}

/* 中文界面：使用 applications/m55/src/edgi_font_ui_cn_16.c（lv_font_conv + 黑体子集），勿单独改用内置 Simsun 稀疏字库 */
static const char *edgi_alarm_name(uint8_t t)
{
    static const char *names[] = {"环境", "火警", "敲门", "婴儿", "水开"};
    if (t < sizeof(names) / sizeof(names[0]))
        return names[t];
    return "?";
}

static const char *edgi_wifi_state_name(uint8_t s)
{
    switch (s)
    {
    case EDGI_WIFI_SHM_IDLE:
        return "WiFi idle";
    case EDGI_WIFI_SHM_DRV:
        return "WiFi init";
    case EDGI_WIFI_SHM_JOIN:
        return "WiFi joining";
    case EDGI_WIFI_SHM_READY:
        return "WiFi online";
    case EDGI_WIFI_SHM_STABLE:
        return "WiFi ready";
    case EDGI_WIFI_SHM_AUTO_ARMED:
        return "WiFi armed";
    case EDGI_WIFI_SHM_AUTO_DELAY:
        return "WiFi wait";
    case EDGI_WIFI_SHM_STARTING:
        return "WiFi starting";
    case EDGI_WIFI_SHM_PC_WAIT:
        return "WiFi PC setup";
    case EDGI_WIFI_SHM_RECONNECT:
        return "WiFi reconnect";
    case EDGI_WIFI_SHM_THREAD_FAIL:
        return "WiFi error";
    case EDGI_WIFI_SHM_START_FAIL:
        return "WiFi error";
    case EDGI_WIFI_SHM_JOIN_FAIL:
        return "WiFi failed";
    default:
        return "WiFi unknown";
    }
}

static const char *edgi_upload_state_name(uint8_t s)
{
    switch (s)
    {
    case EDGI_M55_UPLOAD_NONE:
        return "Cloud idle";
    case EDGI_M55_UPLOAD_THREAD:
        return "Cloud wait";
    case EDGI_M55_UPLOAD_AUTO_ON:
        return "Cloud on";
    case EDGI_M55_UPLOAD_POSTING:
        return "Cloud posting";
    case EDGI_M55_UPLOAD_OK:
        return "Cloud OK";
    case EDGI_M55_UPLOAD_FAIL:
        return "Cloud failed";
    default:
        return "Cloud unknown";
    }
}

static void edgi_detail_label_update(const edgi_shm_alarm_result_t *a, uint32_t seq)
{
    static uint32_t last_seq = UINT32_MAX;
    static uint32_t last_timestamp = UINT32_MAX;
    static uint8_t last_conf = 0xffu;
    static uint8_t last_wifi = 0xffu;
    static uint8_t last_upload = 0xffu;
    edgi_m55_runtime_state_t st;
    unsigned conf;
    char buf[160];

    if (s_lbl_detail == RT_NULL || a == RT_NULL)
        return;

    conf = (unsigned)a->confidence;
    if (conf > 100u)
        conf = 100u;

    st = edgi_m55_state_cached();
    if (seq == last_seq && a->timestamp == last_timestamp &&
        a->confidence == last_conf && st.wifi == last_wifi &&
        st.upload == last_upload)
        return;

    last_seq = seq;
    last_timestamp = a->timestamp;
    last_conf = a->confidence;
    last_wifi = st.wifi;
    last_upload = st.upload;
    snprintf(buf, sizeof(buf), "序号=%lu 置信度=%u%% 时间=%lu\n%s | %s",
             (unsigned long)seq,
             conf,
             (unsigned long)a->timestamp,
             edgi_wifi_state_name(st.wifi),
             edgi_upload_state_name(st.upload));
    lv_label_set_text(s_lbl_detail, buf);
}

static void edgi_alarm_history_push(uint8_t alarm_type)
{
    unsigned tsec = (unsigned)(rt_tick_get() / RT_TICK_PER_SECOND);
    unsigned mm = (tsec / 60u) % 60u;
    unsigned ss = tsec % 60u;

    snprintf(s_hist_ring[s_hist_next], sizeof(s_hist_ring[0]),
             "[%02u:%02u] %s", mm, ss, edgi_alarm_name(alarm_type));
    s_hist_next = (uint8_t)((s_hist_next + 1u) % EDGI_LV_ALARM_HISTORY_LINES);
    if (s_hist_count < EDGI_LV_ALARM_HISTORY_LINES)
        s_hist_count++;
}

static void edgi_alarm_history_refresh_label(void)
{
    char buf[384];
    int pos;
    int k;
    int idx = (int)s_hist_next - 1;
    int visible = (int)s_hist_count;

    if (visible > EDGI_LV_ALARM_HISTORY_VISIBLE_LINES)
        visible = EDGI_LV_ALARM_HISTORY_VISIBLE_LINES;
    pos = snprintf(buf, sizeof(buf), "最近记录 mm:ss");
    if (visible == 0)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, " -");

    if (idx < 0)
        idx += EDGI_LV_ALARM_HISTORY_LINES;
    for (k = 0; k < visible && pos < (int)sizeof(buf) - 48; k++)
    {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "\n%s", s_hist_ring[idx]);
        idx--;
        if (idx < 0)
            idx += EDGI_LV_ALARM_HISTORY_LINES;
    }
    lv_label_set_text(s_lbl_history, buf);
}

static void edgi_title_restore_cb(lv_timer_t *tm)
{
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0x000000), 0);
    s_title_flash_timer = RT_NULL;
    lv_timer_del(tm);
}

static void edgi_title_pulse_nonambient(void)
{
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0xC0392B), 0);
    if (s_title_flash_timer != RT_NULL)
    {
        lv_timer_del(s_title_flash_timer);
        s_title_flash_timer = RT_NULL;
    }
    s_title_flash_timer = lv_timer_create(edgi_title_restore_cb, 400, NULL);
}

static int edgi_try_read_stable(edgi_shm_alarm_result_t *out, uint32_t *out_seq)
{
    void *shm_base = (void *)(uintptr_t)EDGI_M33_M55_SHM_SYSRAM_ADDR;
    volatile edgi_m33_m55_shm_t *shm = (volatile edgi_m33_m55_shm_t *)shm_base;
    int attempt;

    for (attempt = 0; attempt < 6; attempt++)
    {
        uint32_t s1;
        uint32_t s2;
        edgi_shm_alarm_result_t a;

        edgi_shm_invalidate_m33_data(shm_base);

        s1 = shm->seq;
        EDGI_M33_M55_DMB_ISH();
        a.alarm_type  = shm->alarm.alarm_type;
        a.confidence  = shm->alarm.confidence;
        a._reserved   = shm->alarm._reserved;
        a.timestamp   = shm->alarm.timestamp;
        EDGI_M33_M55_DMB_ISH();
        s2 = shm->seq;

        if (s1 == s2 && s1 != 0u)
        {
            *out = a;
            *out_seq = s1;
            return 1;
        }
        rt_thread_mdelay(1);
    }
    return 0;
}

static void edgi_alarm_sync_apply(void)
{
    edgi_shm_alarm_result_t a;
    uint32_t seq;
    char hint[80];
    uint8_t seq_changed;
    uint8_t type_changed;

    if (!edgi_try_read_stable(&a, &seq))
    {
        s_shm_read_fail++;
        if ((s_shm_read_fail % 30u) == 1u && s_lbl_detail != RT_NULL)
        {
            snprintf(hint, sizeof(hint), "读 M33 共享区不稳定(%lu) @0x%08X",
                     (unsigned long)s_shm_read_fail,
                     (unsigned)EDGI_M33_M55_SHM_SYSRAM_ADDR);
            lv_label_set_text(s_lbl_detail, hint);
        }
        return;
    }
    if (seq == 0)
        return;
    edgi_detail_label_update(&a, seq);
    seq_changed = (seq != s_last_shown_seq) ? 1u : 0u;
    type_changed = (a.alarm_type != s_last_shown_type) ? 1u : 0u;
    if (!seq_changed && !type_changed)
        return;
    s_last_shown_seq = seq;
    s_shm_read_fail = 0;
    if (!type_changed)
        return;
    s_last_shown_type = a.alarm_type;

    char buf1[48];
    /* 类别非法时带 t= 便于区分「真无此类别」与读到的脏数据（双核 D-Cache 未维护时可能出现） */
    if (a.alarm_type < 5u)
        snprintf(buf1, sizeof(buf1), "%s", edgi_alarm_name(a.alarm_type));
    else
        snprintf(buf1, sizeof(buf1), "? (t=%u)", (unsigned)a.alarm_type);

    lv_label_set_text(s_lbl_title, buf1);

    edgi_apply_alarm_visual(a.alarm_type);

    /* 非环境类：多维提醒之一 — 标题短时强调 + 记入最近记录（上电以来 mm:ss） */
    if (a.alarm_type >= 1u && a.alarm_type < 5u)
    {
        edgi_alarm_history_push(a.alarm_type);
        edgi_title_pulse_nonambient();
    }
    edgi_alarm_history_refresh_label();
}

static void edgi_alarm_sync_timer(lv_timer_t *timer)
{
    edgi_m55_runtime_state_t st;
    uint32_t want;

    if (++s_timer_beat_div >= 10u)
    {
        s_timer_beat_div = 0u;
        edgi_m55_shm_heartbeat();
    }

    st = edgi_m55_state_cached();
    want = (st.wifi >= EDGI_WIFI_SHM_DRV && st.wifi < EDGI_WIFI_SHM_STABLE) ? 500u : 250u;
    if (want != s_shm_poll_ms)
    {
        s_shm_poll_ms = want;
        lv_timer_set_period(timer, want);
    }
    edgi_alarm_sync_apply();
}

void edgi_lv_alarm_from_m33_init(void)
{
    rt_kprintf("[M55] edgi_lv_alarm_from_m33_init (LVGL thread, alarm UI)\r\n");
    edgi_m55_shm_set_ui_state(EDGI_M55_UI_ALARM_INIT);
    /* 敲门：内置 lv_font_montserrat_24 未编入 FA 的 door-open(U+F52B)，用语义相近的门铃图标 */
    static const char *icon_texts[EDGI_SLOT_COUNT] = {
        LV_SYMBOL_BELL,
        LV_SYMBOL_WARNING,
        "Q_Q",
        LV_SYMBOL_TINT};
    static const char *status_texts[EDGI_SLOT_COUNT] = {
        "敲门",
        "火灾告警",
        "婴儿啼哭",
        "烧水沸腾"};
    uint8_t i;
    lv_obj_t *scr;
    lv_coord_t scr_w, scr_h;
    lv_coord_t panel_top, panel_h;
    lv_coord_t gap, card_w, card_h;

    // 检查LVGL是否初始化
    if(!lv_is_initialized()) {
        edgi_m55_shm_set_ui_state(EDGI_M55_UI_NOT_INIT);
        rt_kprintf("[M55] LVGL not initialized!\r\n");
        return;
    }

    scr = lv_screen_active();
    if(!scr) {
        edgi_m55_shm_set_ui_state(EDGI_M55_UI_NO_SCREEN);
        rt_kprintf("[M55] No active screen!\r\n");
        return;
    }
    s_scr_root = scr;

    edgi_ui_prepare_styles();
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &s_style_bg, 0);
    scr_w = lv_obj_get_width(scr);
    scr_h = lv_obj_get_height(scr);
    if (scr_w <= 0)
        scr_w = lv_display_get_horizontal_resolution(NULL);
    if (scr_h <= 0)
        scr_h = lv_display_get_vertical_resolution(NULL);
    if (scr_w <= 0 || scr_h <= 0)
    {
        edgi_m55_shm_set_ui_state(EDGI_M55_UI_BAD_SIZE);
        rt_kprintf("[M55] LVGL screen size invalid: %ld x %ld\r\n",
                   (long)scr_w, (long)scr_h);
        return;
    }

    /* 创建标题标签（收到 M33 数据后会改为中文类别名；alarm_type=0 时为环境/低功耗） */
    s_lbl_title = lv_label_create(scr);
    lv_label_set_text(s_lbl_title, "环境 / 待机");
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(s_lbl_title, &edgi_font_ui_cn_16, 0);

    panel_top = 48;
    panel_h = scr_h - panel_top - 120;
    if (panel_h < 120)
        panel_h = 120;
    gap = 8;
    card_w = (scr_w - gap * 3) / 2;
    card_h = (panel_h - gap * 3) / 2;
    if (card_w < 90)
        card_w = 90;
    if (card_h < 52)
        card_h = 52;

    for (i = 0; i < EDGI_SLOT_COUNT; i++)
    {
        lv_coord_t x = gap + (i % 2) * (card_w + gap);
        lv_coord_t y = panel_top + gap + (i / 2) * (card_h + gap);

        s_status_cards[i] = lv_obj_create(scr);
        lv_obj_set_size(s_status_cards[i], card_w, card_h);
        lv_obj_set_pos(s_status_cards[i], x, y);
        lv_obj_add_style(s_status_cards[i], &s_style_card_normal, 0);
        lv_obj_clear_flag(s_status_cards[i], LV_OBJ_FLAG_SCROLLABLE);

        s_status_icons[i] = lv_label_create(s_status_cards[i]);
        lv_label_set_text(s_status_icons[i], icon_texts[i]);
        lv_obj_add_style(s_status_icons[i], &s_style_icon_normal, 0);
        lv_obj_align(s_status_icons[i], LV_ALIGN_CENTER, 0, -6);

        s_status_texts[i] = lv_label_create(s_status_cards[i]);
        lv_label_set_text(s_status_texts[i], status_texts[i]);
        lv_obj_set_style_text_color(s_status_texts[i], lv_color_hex(0x1E2A36), 0);
        lv_obj_set_style_text_font(s_status_texts[i], &edgi_font_ui_cn_16, 0);
        lv_obj_align(s_status_texts[i], LV_ALIGN_BOTTOM_MID, 0, -6);
    }

    edgi_apply_alarm_visual(0u);

    /* 最近告警（中文 + 上电 mm:ss）；可与卡片高亮、钉钉推送形成「视 + 远端」多维提醒 */
    s_lbl_history = lv_label_create(scr);
    lv_label_set_text(s_lbl_history, "");
    lv_obj_set_style_text_color(s_lbl_history, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(s_lbl_history, &edgi_font_ui_cn_16, 0);
    lv_label_set_long_mode(s_lbl_history, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_history, scr_w > 24 ? scr_w - 24 : 280);
    lv_obj_align(s_lbl_history, LV_ALIGN_BOTTOM_MID, 0, -54);
    edgi_alarm_history_refresh_label();

    /* 创建详情标签 */
    s_lbl_detail = lv_label_create(scr);
    lv_label_set_text(s_lbl_detail, "等待 M33 告警数据...");
    lv_obj_align(s_lbl_detail, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_color(s_lbl_detail, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(s_lbl_detail, &edgi_font_ui_cn_16, 0);
    lv_label_set_long_mode(s_lbl_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_detail, scr_w > 24 ? scr_w - 24 : 300);

    /* 创建定时器，每100ms轮询一次 */
    lv_timer_create(edgi_alarm_sync_timer, 250, NULL);

    /* 立即执行一次同步 */
    edgi_alarm_sync_apply();
    lv_obj_invalidate(scr);
    lv_refr_now(NULL);
    edgi_m55_shm_set_ui_state(EDGI_M55_UI_ALARM_READY);
    edgi_m55_shm_heartbeat();
    rt_kprintf("[M55] alarm UI ready: %ld x %ld\r\n", (long)scr_w, (long)scr_h);
}

void lv_user_gui_init(void)
{
    edgi_lv_alarm_from_m33_init();
}
