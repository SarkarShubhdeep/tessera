/* Tessera surface: the orb (presence) and the plate deck.
 *
 * Speed rules for the board's QSPI display: true black background, solid shapes, and continuous
 * animation only inside small regions (the ring lives in a fixed 300 x 300 box). Full-screen moves
 * (plate enter, swipe) are short: see TS_MS_* in tokens.h. */
#include "ui.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "plates.h"
#include "tokens.h"

#define SEG_N       24     /* ring segments */
#define RING_BOX    300    /* the ring never draws outside this box (canvas units) */
#define DECK_MAX    8
#define RING_PI     3.14159265f

static lv_obj_t *s_scr;
static lv_obj_t *s_orb_layer;
static lv_obj_t *s_ring;
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

static void anim_opa(void *o, int32_t v) { lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }
static void anim_tx(void *o, int32_t v) { lv_obj_set_style_translate_x(o, v, 0); }
static void anim_ty(void *o, int32_t v) { lv_obj_set_style_translate_y(o, v, 0); }

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

/* ---------- presence: the ring ----------
 *
 * 24 square segments around a circle (decision 008). Every state is a set of targets for each
 * segment's length, width and brightness plus the ring's radius; a 60 Hz timer eases the current
 * values toward them, so a state change is always a morph, never a swap. Numbers are canvas units
 * (480 wide), the same as the landing page's ring. */

typedef struct {
    float len, w, a;
} seg_t;

static seg_t s_seg[SEG_N];
static float s_radius = 64.0f;
static float s_inward;             /* 0 = segments grow outward, 1 = inward (listening) */
static float s_level;              /* smoothed audio level, 0..1 */
static uint8_t s_level_in;         /* latest ui_set_level() */
static uint32_t s_last_ms;
static lv_timer_t *s_ring_timer;

void ui_set_level(uint8_t level) { s_level_in = level; }

static float approach(float cur, float target, float k) { return cur + (target - cur) * k; }

static void ring_tick(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    float dt = (float)lv_tick_diff(now, s_last_ms) / 1000.0f;
    s_last_ms = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    float k = 1.0f - expf(-dt * 14.0f);    /* ~70 ms time constant */
    float ka = 1.0f - expf(-dt * 40.0f);   /* brightness is quicker, so the thinking arc stays bright */
    float tm = (float)(now % 600000u) / 1000.0f;
    s_level = approach(s_level, s_level_in / 255.0f, 1.0f - expf(-dt * 25.0f));

    float radius = 64.0f, width = 12.0f;
    switch (s_state) {
    case UI_STATE_IDLE:      radius = 64.0f + 3.0f * sinf(tm * 2.0f * RING_PI / 3.0f); width = 16.9f; break;
    case UI_STATE_LISTENING: radius = 100.0f; break;   /* the ring opens out: wide gaps between segments */
    case UI_STATE_SPEAKING:  radius = 70.0f; break;
    default: break;
    }
    s_radius = approach(s_radius, radius, k);
    s_inward = approach(s_inward, s_state == UI_STATE_LISTENING ? 1.0f : 0.0f, k);

    float head = fmodf(tm * 0.85f, 1.0f) * SEG_N;
    for (int i = 0; i < SEG_N; i++) {
        float len = 12.0f, a = 1.0f;
        if (s_state == UI_STATE_LISTENING) {          /* your voice comes in: spikes point inward, uneven */
            float lvl = 0.12f + 0.88f * s_level;
            len = 12.0f + 52.0f * lvl * (0.35f + 0.65f * fabsf(sinf(i * 1.9f + tm * 5.3f)));
        } else if (s_state == UI_STATE_SPEAKING) {    /* its voice: symmetric about the top */
            float lvl = 0.12f + 0.88f * s_level;
            float m = (float)(i < SEG_N - i ? i : SEG_N - i) / (SEG_N / 2);
            len = 12.0f + 56.0f * lvl * (0.4f + 0.6f * fabsf(sinf(m * 5.1f + tm * 7.7f)));
        } else if (s_state == UI_STATE_THINKING) {    /* a lit arc circling, the one non-audio motion */
            float d = fmodf(head - i + SEG_N, SEG_N);
            a = fmaxf(0.22f, 1.0f - d / 13.0f);
            len = d < 2.5f ? 22.0f : 12.0f;
        }
        s_seg[i].len = approach(s_seg[i].len, len, k);
        s_seg[i].w = approach(s_seg[i].w, width, k);
        s_seg[i].a = approach(s_seg[i].a, a, ka);
    }
    if (!lv_obj_has_flag(s_orb_layer, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_invalidate(s_ring);
    }
}

/* Each segment is a thick line with flat ends: one anti-aliased rotated rectangle. */
static void ring_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(s_ring, &c);
    float scale = (float)lv_obj_get_width(s_ring) / RING_BOX;
    float cx = c.x1 + lv_area_get_width(&c) / 2.0f;
    float cy = c.y1 + lv_area_get_height(&c) / 2.0f;

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = TS_ACCENT;
    for (int i = 0; i < SEG_N; i++) {
        float ang = (float)i / SEG_N * 2.0f * RING_PI - RING_PI / 2.0f;
        float ux = cosf(ang), uy = sinf(ang);
        /* Outward, a segment runs from the radius out; inward, its outer end stays put and the extra
         * length grows toward the centre. s_inward morphs between the two. */
        float inner = s_radius - s_inward * (s_seg[i].len - 12.0f);
        float r0 = inner * scale, r1 = (inner + s_seg[i].len) * scale;
        d.p1.x = (int32_t)lroundf(cx + ux * r0);
        d.p1.y = (int32_t)lroundf(cy + uy * r0);
        d.p2.x = (int32_t)lroundf(cx + ux * r1);
        d.p2.y = (int32_t)lroundf(cy + uy * r1);
        d.width = (int32_t)lroundf(s_seg[i].w * scale);
        d.opa = (lv_opa_t)lroundf(s_seg[i].a * 255.0f);
        lv_draw_line(layer, &d);
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
    lv_timer_resume(s_ring_timer);
    s_state = state;
    lv_label_set_text(s_status, STATUS_TEXT[state]);
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
    lv_timer_pause(s_ring_timer);   /* the ring is hidden under a plate: no work */
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

    /* The ring: one object in a fixed 300 x 300 box, so its animation never redraws outside it. */
    s_ring = lv_obj_create(s_orb_layer);
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, TS_PX(RING_BOX), TS_PX(RING_BOX));
    lv_obj_align(s_ring, LV_ALIGN_CENTER, 0, TS_PX(-4));
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ring, ring_draw, LV_EVENT_DRAW_MAIN, NULL);
    for (int i = 0; i < SEG_N; i++) {
        s_seg[i] = (seg_t){12.0f, 16.9f, 1.0f};
    }
    s_last_ms = lv_tick_get();
    s_ring_timer = lv_timer_create(ring_tick, 16, NULL);

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
