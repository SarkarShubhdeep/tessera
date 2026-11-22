/* Tessera surface: the orb (presence) and the plate deck.
 *
 * Speed rules for the board's QSPI display: true black background, solid shapes, and continuous
 * animation only inside small regions (the orb lives in a fixed 200 x 200 box). Full-screen moves
 * (plate enter, swipe) are short: see TS_MS_* in tokens.h. */
#include "ui.h"

#include <stdlib.h>
#include <string.h>

#include "plates.h"
#include "tokens.h"

#define ORB_SIZE    176
#define BAR_COUNT   5
#define DOT_COUNT   3
#define DECK_MAX    8

static lv_obj_t *s_scr;
static lv_obj_t *s_orb_layer;
static lv_obj_t *s_orb;
static lv_obj_t *s_bars[BAR_COUNT];
static lv_obj_t *s_dots[DOT_COUNT];
static lv_obj_t *s_status;
static lv_obj_t *s_clock;
static lv_obj_t *s_plate;          /* the plate on screen, or NULL */
static lv_obj_t *s_detents;
static ui_state_t s_state = UI_STATE_IDLE;

static cJSON *s_deck[DECK_MAX];    /* plate specs, oldest first */
static int s_deck_n;
static int s_pos = -1;             /* -1 = orb */

static const char *const STATUS_TEXT[UI_STATE_COUNT] = {
    "Tap to talk", "Listening", "Thinking", "Speaking",
};

/* ---------- animation helpers ---------- */

static void anim_size(void *o, int32_t v) { lv_obj_set_size(o, v, v); }
static void anim_height(void *o, int32_t v) { lv_obj_set_height(o, v); }
static void anim_bg_opa(void *o, int32_t v) { lv_obj_set_style_bg_opa(o, (lv_opa_t)v, 0); }
static void anim_opa(void *o, int32_t v) { lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }
static void anim_tx(void *o, int32_t v) { lv_obj_set_style_translate_x(o, v, 0); }
static void anim_ty(void *o, int32_t v) { lv_obj_set_style_translate_y(o, v, 0); }

static void loop(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay,
                 lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_reverse_duration(&a, ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static void once(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
                 lv_anim_path_cb_t path, lv_anim_completed_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path);
    if (done) {
        lv_anim_set_completed_cb(&a, done);
    }
    lv_anim_start(&a);
}

static void show(lv_obj_t *obj, bool visible)
{
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------- detents: one tick per deck position, the current one lit ---------- */

static void draw_detents(void)
{
    lv_obj_clean(s_detents);
    show(s_detents, s_deck_n > 0);
    for (int i = -1; i < s_deck_n; i++) {
        lv_obj_t *t = lv_obj_create(s_detents);
        lv_obj_remove_style_all(t);
        lv_obj_set_size(t, TS_PX(26), TS_PX(4));
        lv_obj_set_style_bg_color(t, i == s_pos ? TS_INK : TS_RULE, 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    }
    lv_obj_move_foreground(s_detents);
}

/* ---------- orb ---------- */

static void stop_orb(void)
{
    lv_anim_delete(s_orb, NULL);
    for (int i = 0; i < BAR_COUNT; i++) {
        lv_anim_delete(s_bars[i], NULL);
    }
    for (int i = 0; i < DOT_COUNT; i++) {
        lv_anim_delete(s_dots[i], NULL);
    }
}

static void remove_plate(bool animate, int dir);

void ui_set_state(ui_state_t state)
{
    if (state >= UI_STATE_COUNT) {
        return;
    }
    if (s_plate) {
        remove_plate(false, 0);
        s_pos = -1;
        draw_detents();
    }
    show(s_orb_layer, true);
    s_state = state;
    stop_orb();
    lv_label_set_text(s_status, STATUS_TEXT[state]);

    show(s_orb, state == UI_STATE_IDLE || state == UI_STATE_SPEAKING);
    for (int i = 0; i < BAR_COUNT; i++) {
        show(s_bars[i], state == UI_STATE_LISTENING);
    }
    for (int i = 0; i < DOT_COUNT; i++) {
        show(s_dots[i], state == UI_STATE_THINKING);
    }

    switch (state) {
    case UI_STATE_IDLE: /* slow breathing */
        loop(s_orb, anim_size, TS_PX(ORB_SIZE - 16), TS_PX(ORB_SIZE), 2400, 0, lv_anim_path_ease_in_out);
        break;
    case UI_STATE_LISTENING: /* level bars out of phase */
        for (int i = 0; i < BAR_COUNT; i++) {
            loop(s_bars[i], anim_height, TS_PX(28), TS_PX(120 - (i % 2) * 30), 320 + i * 40, i * 90,
                 lv_anim_path_ease_in_out);
        }
        break;
    case UI_STATE_THINKING: /* dots in turn */
        for (int i = 0; i < DOT_COUNT; i++) {
            loop(s_dots[i], anim_bg_opa, LV_OPA_20, LV_OPA_COVER, 420, i * 180, lv_anim_path_ease_in_out);
        }
        break;
    case UI_STATE_SPEAKING: /* quick pulse */
        loop(s_orb, anim_size, TS_PX(ORB_SIZE - 30), TS_PX(ORB_SIZE + 10), 260, 0, lv_anim_path_ease_out);
        break;
    default:
        break;
    }
}

/* ---------- plates and the deck ---------- */

static void delete_when_done(lv_anim_t *a)
{
    lv_obj_delete_async(a->var);
}

static void remove_plate(bool animate, int dir)
{
    if (!s_plate) {
        return;
    }
    lv_obj_t *old = s_plate;
    s_plate = NULL;
    if (!animate) {
        lv_obj_delete(old);
        return;
    }
    /* Exit: half the entrance, ease-in, sliding away from the incoming plate. */
    once(old, anim_tx, 0, TS_PX(-TS_SWIPE_SHIFT) * dir, TS_MS_EXIT, lv_anim_path_ease_in, NULL);
    once(old, anim_opa, LV_OPA_COVER, LV_OPA_TRANSP, TS_MS_EXIT, lv_anim_path_ease_in, delete_when_done);
}

/* dir: 0 = new plate rises in; +1/-1 = slides in from the right/left */
static void put_plate(const cJSON *spec, bool animate, int dir)
{
    remove_plate(animate && dir != 0, dir);
    stop_orb();
    show(s_orb_layer, false);
    s_plate = ts_plate_create(s_scr, spec);
    if (animate) {
        if (dir == 0) {
            once(s_plate, anim_ty, TS_PX(TS_ENTER_RISE), 0, TS_MS_ENTER, lv_anim_path_ease_out, NULL);
            once(s_plate, anim_opa, LV_OPA_TRANSP, LV_OPA_COVER, TS_MS_ENTER, lv_anim_path_ease_out, NULL);
        } else {
            once(s_plate, anim_tx, TS_PX(TS_SWIPE_SHIFT) * dir, 0, TS_MS_SWIPE, lv_anim_path_ease_out, NULL);
            once(s_plate, anim_opa, LV_OPA_TRANSP, LV_OPA_COVER, TS_MS_SWIPE, lv_anim_path_ease_out, NULL);
        }
    }
    draw_detents();
}

void ui_show_plate(const cJSON *spec, bool animate)
{
    if (s_deck_n == DECK_MAX) { /* drop the oldest */
        cJSON_Delete(s_deck[0]);
        memmove(&s_deck[0], &s_deck[1], sizeof(s_deck[0]) * (DECK_MAX - 1));
        s_deck_n--;
    }
    s_deck[s_deck_n++] = cJSON_Duplicate(spec, true);
    s_pos = s_deck_n - 1;
    put_plate(spec, animate, 0);
}

void ui_swipe(int dir)
{
    int next = s_pos + (dir > 0 ? 1 : -1);
    if (next < -1 || next >= s_deck_n) {
        return;
    }
    s_pos = next;
    if (s_pos < 0) {
        remove_plate(true, dir);
        ui_set_state(UI_STATE_IDLE);
    } else {
        put_plate(s_deck[s_pos], true, dir);
    }
}

int ui_deck_size(void) { return s_deck_n; }
int ui_deck_pos(void) { return s_pos; }

/* ---------- input: tap cycles the orb; swipe and arrow keys move through the deck ---------- */

static void on_tap(lv_event_t *e)
{
    (void)e;
    if (s_pos < 0) {
        ui_set_state((ui_state_t)((s_state + 1) % UI_STATE_COUNT));
    }
}

static void on_gesture(lv_event_t *e)
{
    (void)e;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_active());
    if (d == LV_DIR_LEFT) {
        ui_swipe(+1);
    } else if (d == LV_DIR_RIGHT) {
        ui_swipe(-1);
    }
}

static void on_key(lv_event_t *e)
{
    uint32_t k = lv_event_get_key(e);
    if (k == LV_KEY_RIGHT) {
        ui_swipe(+1);
    } else if (k == LV_KEY_LEFT) {
        ui_swipe(-1);
    }
}

/* ---------- clock (the board will use SNTP time; the simulator fakes it) ---------- */

static void clock_tick(lv_timer_t *t)
{
    (void)t;
    uint32_t s = lv_tick_get() / 1000 + 9 * 3600 + 41 * 60;
    lv_label_set_text_fmt(s_clock, "%02u:%02u", (unsigned)((s / 3600) % 24), (unsigned)((s / 60) % 60));
}

/* ---------- build ---------- */

static lv_obj_t *shape(lv_obj_t *parent, int32_t w, int32_t h, int32_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, TS_PX(w), TS_PX(h));
    lv_obj_set_style_radius(o, radius == LV_RADIUS_CIRCLE ? radius : TS_PX(radius), 0);
    lv_obj_set_style_bg_color(o, TS_ACCENT, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void ui_create(void)
{
    s_scr = lv_screen_active();
    lv_obj_set_style_bg_color(s_scr, TS_BLACK, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_scr, on_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_scr, on_key, LV_EVENT_KEY, NULL);

    s_orb_layer = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_orb_layer);
    lv_obj_set_size(s_orb_layer, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_orb_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_clock = lv_label_create(s_orb_layer);
    lv_obj_set_style_text_font(s_clock, TS_FONT_SUB, 0);
    lv_obj_set_style_text_color(s_clock, TS_DIM, 0);
    lv_obj_set_style_text_letter_space(s_clock, TS_TRACK_LABEL, 0);
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, TS_PX(40));
    lv_timer_create(clock_tick, 1000, NULL);
    clock_tick(NULL);

    /* The stage: a fixed 200 x 200 box, so orb animation never redraws outside it. */
    lv_obj_t *stage = lv_obj_create(s_orb_layer);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, TS_PX(200), TS_PX(200));
    lv_obj_align(stage, LV_ALIGN_CENTER, 0, TS_PX(-10));
    lv_obj_remove_flag(stage, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_orb = shape(stage, ORB_SIZE, ORB_SIZE, LV_RADIUS_CIRCLE);
    lv_obj_center(s_orb);
    for (int i = 0; i < BAR_COUNT; i++) {
        s_bars[i] = shape(stage, 22, 40, 11);
        lv_obj_align(s_bars[i], LV_ALIGN_CENTER, TS_PX((i - BAR_COUNT / 2) * 36), 0);
    }
    for (int i = 0; i < DOT_COUNT; i++) {
        s_dots[i] = shape(stage, 30, 30, LV_RADIUS_CIRCLE);
        lv_obj_align(s_dots[i], LV_ALIGN_CENTER, TS_PX((i - DOT_COUNT / 2) * 52), 0);
    }

    s_status = lv_label_create(s_orb_layer);
    lv_obj_set_style_text_font(s_status, TS_FONT_SUB, 0);
    lv_obj_set_style_text_color(s_status, TS_INK, 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, TS_PX(-70));

    s_detents = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_detents);
    lv_obj_set_size(s_detents, LV_SIZE_CONTENT, TS_PX(4));
    lv_obj_set_flex_flow(s_detents, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_detents, TS_PX(8), 0);
    lv_obj_align(s_detents, LV_ALIGN_BOTTOM_MID, 0, TS_PX(-10));
    lv_obj_remove_flag(s_detents, LV_OBJ_FLAG_CLICKABLE);

    draw_detents();
    ui_set_state(UI_STATE_IDLE);
}
