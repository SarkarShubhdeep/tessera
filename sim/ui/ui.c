/* Mosaico home screen: an agent "presence" for a 480x480 AMOLED.
 *
 * Design rules for speed on the board (QSPI display, partial redraws):
 *  - true black background: on AMOLED black pixels are off, and black areas never need redrawing;
 *  - solid shapes, no full-screen blur, gradients or transparency;
 *  - every animation stays inside a small box, so each frame only redraws that box.
 */
#include "ui.h"

#define ACCENT      lv_color_hex(0xFF5A1F)  /* Mosaico orange (same accent as the teardown) */
#define INK         lv_color_hex(0xF2EEE8)
#define INK_DIM     lv_color_hex(0x6B655E)
#define BLACK       lv_color_hex(0x000000)

#define ORB_SIZE    176
#define BAR_COUNT   5
#define DOT_COUNT   3

static lv_obj_t *s_orb;
static lv_obj_t *s_bars[BAR_COUNT];
static lv_obj_t *s_dots[DOT_COUNT];
static lv_obj_t *s_status;
static lv_obj_t *s_clock;
static ui_state_t s_state = UI_STATE_IDLE;

static const char *const STATUS_TEXT[UI_STATE_COUNT] = {
    "Tap to talk", "Listening", "Thinking", "Speaking",
};

/* ---------- animation helpers ---------- */

static void set_size_cb(void *obj, int32_t v)
{
    lv_obj_set_size(obj, v, v);
}

static void set_height_cb(void *obj, int32_t v)
{
    lv_obj_set_height(obj, v);
}

static void set_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)v, 0);
}

static void start_anim(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to,
                       uint32_t ms, uint32_t delay, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_playback_duration(&a, ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static void stop_all(void)
{
    lv_anim_delete(s_orb, NULL);
    for (int i = 0; i < BAR_COUNT; i++) {
        lv_anim_delete(s_bars[i], NULL);
    }
    for (int i = 0; i < DOT_COUNT; i++) {
        lv_anim_delete(s_dots[i], NULL);
    }
}

static void show(lv_obj_t *obj, bool visible)
{
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------- state ---------- */

void ui_set_state(ui_state_t state)
{
    if (state >= UI_STATE_COUNT) {
        return;
    }
    s_state = state;
    stop_all();
    lv_label_set_text(s_status, STATUS_TEXT[state]);

    const bool orb = (state == UI_STATE_IDLE || state == UI_STATE_SPEAKING);
    show(s_orb, orb);
    for (int i = 0; i < BAR_COUNT; i++) {
        show(s_bars[i], state == UI_STATE_LISTENING);
    }
    for (int i = 0; i < DOT_COUNT; i++) {
        show(s_dots[i], state == UI_STATE_THINKING);
    }

    switch (state) {
    case UI_STATE_IDLE: /* slow breathing */
        lv_obj_set_style_bg_opa(s_orb, LV_OPA_COVER, 0);
        start_anim(s_orb, set_size_cb, ORB_SIZE - 16, ORB_SIZE, 2400, 0, lv_anim_path_ease_in_out);
        break;
    case UI_STATE_LISTENING: /* bars bounce out of phase, like a level meter */
        for (int i = 0; i < BAR_COUNT; i++) {
            start_anim(s_bars[i], set_height_cb, 28, 120 - (i % 2) * 30, 320 + i * 40, i * 90,
                       lv_anim_path_ease_in_out);
        }
        break;
    case UI_STATE_THINKING: /* dots fade one after another */
        for (int i = 0; i < DOT_COUNT; i++) {
            start_anim(s_dots[i], set_opa_cb, LV_OPA_20, LV_OPA_COVER, 420, i * 180, lv_anim_path_ease_in_out);
        }
        break;
    case UI_STATE_SPEAKING: /* quick pulse */
        lv_obj_set_style_bg_opa(s_orb, LV_OPA_COVER, 0);
        start_anim(s_orb, set_size_cb, ORB_SIZE - 30, ORB_SIZE + 10, 260, 0, lv_anim_path_ease_out);
        break;
    default:
        break;
    }
}

static void on_tap(lv_event_t *e)
{
    (void)e;
    ui_set_state((ui_state_t)((s_state + 1) % UI_STATE_COUNT));
}

/* ---------- clock (the simulator fakes time; the board will use the RTC/SNTP time) ---------- */

static void clock_tick(lv_timer_t *t)
{
    (void)t;
    uint32_t s = lv_tick_get() / 1000 + 9 * 3600 + 41 * 60;
    lv_label_set_text_fmt(s_clock, "%02u:%02u", (unsigned)((s / 3600) % 24), (unsigned)((s / 60) % 60));
}

/* ---------- build ---------- */

static lv_obj_t *make_shape(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, ACCENT, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, BLACK, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_tap, LV_EVENT_CLICKED, NULL);

    s_clock = lv_label_create(scr);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_clock, INK_DIM, 0);
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, 36);
    lv_timer_create(clock_tick, 1000, NULL);
    clock_tick(NULL);

    /* Centre stage: a fixed 200x200 box, so animations never redraw outside it. */
    lv_obj_t *stage = lv_obj_create(scr);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, 200, 200);
    lv_obj_align(stage, LV_ALIGN_CENTER, 0, -10);
    lv_obj_remove_flag(stage, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_orb = make_shape(stage, ORB_SIZE, ORB_SIZE, LV_RADIUS_CIRCLE);
    lv_obj_center(s_orb);

    for (int i = 0; i < BAR_COUNT; i++) {
        s_bars[i] = make_shape(stage, 22, 40, 11);
        lv_obj_align(s_bars[i], LV_ALIGN_CENTER, (i - BAR_COUNT / 2) * 36, 0);
    }
    for (int i = 0; i < DOT_COUNT; i++) {
        s_dots[i] = make_shape(stage, 30, 30, LV_RADIUS_CIRCLE);
        lv_obj_align(s_dots[i], LV_ALIGN_CENTER, (i - DOT_COUNT / 2) * 52, 0);
    }

    s_status = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_status, INK, 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -70);

    ui_set_state(UI_STATE_IDLE);
}
