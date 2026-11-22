/* Plate builders. Every plate shares one frame: a header row, a body, and an optional footer, separated
 * by 2 px rules, inside a 36-unit margin. Everything is flat: no radius, no shadow, no gradient. */
#include "plates.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokens.h"

#define LIST_MAX     5
#define OPTIONS_MAX  4
#define SEGMENTS     10
#define LONG_ANSWER  64   /* characters; longer answers drop to the row font */
#define LONG_QUESTION 40

int32_t ts_px(int32_t v)
{
    lv_display_t *d = lv_display_get_default();
    int32_t w = d ? lv_display_get_horizontal_resolution(d) : TS_CANVAS;
    return (v * w + TS_CANVAS / 2) / TS_CANVAS;
}

/* ---------- small helpers ---------- */

static const char *jstr(const cJSON *o, const char *key, const char *fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : fallback;
}

static double jnum(const cJSON *o, const char *key, double fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

static void upper(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    for (; src && src[i] && i + 1 < n; i++) {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

/* Value text: integers without decimals, otherwise one decimal; strings pass through. */
static void value_text(char *dst, size_t n, const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsNumber(v)) {
        double d = v->valuedouble;
        if (fabs(d - round(d)) < 1e-9) {
            snprintf(dst, n, "%ld", (long)round(d));
        } else {
            snprintf(dst, n, "%.1f", d);
        }
    } else if (cJSON_IsString(v) && v->valuestring) {
        snprintf(dst, n, "%s", v->valuestring);
    } else {
        snprintf(dst, n, "-");
    }
}

/* The pixel hero font only carries numerals and a few marks. */
static bool fits_hero_font(const char *s)
{
    for (; *s; s++) {
        if (!strchr("0123456789:.%+- ", *s)) {
            return false;
        }
    }
    return true;
}

static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *label_caps(lv_obj_t *parent, const char *s, const lv_font_t *font, lv_color_t color)
{
    char buf[96];
    upper(buf, sizeof(buf), s);
    lv_obj_t *l = text(parent, buf, font, color);
    lv_obj_set_style_text_letter_space(l, TS_TRACK_LABEL, 0);
    return l;
}

static void rule(lv_obj_t *o, lv_border_side_t side)
{
    lv_obj_set_style_border_side(o, side, 0);
    lv_obj_set_style_border_width(o, TS_PX(TS_RULE_W), 0);
    lv_obj_set_style_border_color(o, TS_RULE, 0);
}

static lv_obj_t *row(lv_obj_t *parent, lv_flex_align_t main, lv_flex_align_t cross)
{
    lv_obj_t *r = box(parent);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, main, cross, cross);
    return r;
}

/* A sharp accent square: the "live" marker and the selected-option mark. */
static lv_obj_t *square(lv_obj_t *parent, int32_t size, lv_color_t color)
{
    lv_obj_t *s = box(parent);
    lv_obj_set_size(s, TS_PX(size), TS_PX(size));
    lv_obj_set_style_bg_color(s, color, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    return s;
}

/* ---------- the shared frame ---------- */

typedef struct {
    lv_obj_t *root;
    lv_obj_t *head_right;
    lv_obj_t *body;
} frame_t;

static frame_t frame(lv_obj_t *parent, const char *left, const char *right, bool live)
{
    frame_t f;
    f.root = box(parent);
    lv_obj_set_size(f.root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(f.root, TS_BLACK, 0);
    lv_obj_set_style_bg_opa(f.root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(f.root, TS_PX(TS_MARGIN), 0);
    lv_obj_set_flex_flow(f.root, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *head = row(f.root, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(head, TS_PX(TS_GAP_M), 0);
    rule(head, LV_BORDER_SIDE_BOTTOM);
    label_caps(head, left, TS_FONT_HEAD, TS_DIM);

    lv_obj_t *right_box = row(head, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(right_box, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(right_box, TS_PX(10), 0);
    if (live) {
        square(right_box, 10, TS_ACCENT);
    }
    f.head_right = label_caps(right_box, right ? right : "", TS_FONT_HEAD, live ? TS_ACCENT : TS_DIM);

    f.body = box(f.root);
    lv_obj_set_width(f.body, lv_pct(100));
    lv_obj_set_flex_grow(f.body, 1);
    lv_obj_set_flex_flow(f.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(f.body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return f;
}

static void footer(const frame_t *f, const char *left, const char *right)
{
    lv_obj_t *foot = row(f->root, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(foot, TS_PX(TS_GAP_M), 0);
    rule(foot, LV_BORDER_SIDE_TOP);
    label_caps(foot, left, TS_FONT_FOOT, TS_DIM);
    label_caps(foot, right, TS_FONT_FOOT, TS_INK);
}

/* Hero value with an optional unit beside it, set on the numerals' baseline area. */
static lv_obj_t *hero(lv_obj_t *parent, const char *value, const char *unit)
{
    lv_obj_t *r = row(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(r, TS_PX(8), 0);
    const lv_font_t *font = fits_hero_font(value) ? TS_FONT_HERO : TS_FONT_LEAD;
    lv_obj_t *v = text(r, value, font, TS_INK);
    if (unit && unit[0]) {
        lv_obj_t *u = text(r, unit, TS_FONT_LEAD, TS_DIM);
        lv_obj_set_style_pad_bottom(u, TS_PX(font == TS_FONT_HERO ? 18 : 0), 0);
    }
    return v;
}

static lv_obj_t *bar(lv_obj_t *parent, int32_t height, int32_t pct)
{
    lv_obj_t *track = box(parent);
    lv_obj_set_size(track, lv_pct(100), TS_PX(height));
    lv_obj_set_style_bg_color(track, TS_RULE, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_top(track, TS_PX(TS_GAP_L), 0);
    lv_obj_t *fill = box(track);
    lv_obj_set_size(fill, lv_pct(pct), lv_pct(100));
    lv_obj_set_style_bg_color(fill, TS_ACCENT, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    return fill;
}

static void sub(lv_obj_t *parent, const char *s)
{
    if (!s || !s[0]) {
        return;
    }
    lv_obj_t *l = text(parent, s, TS_FONT_SUB, TS_DIM);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_margin_top(l, TS_PX(14), 0);
}

/* ---------- timer: counts down on the device itself ---------- */

typedef struct {
    int32_t total;
    int32_t left;
    lv_obj_t *value;
    lv_obj_t *fill;
    lv_obj_t *status;
    lv_timer_t *tick;
} timer_state_t;

static void timer_paint(timer_state_t *t)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld:%02ld", (long)(t->left / 60), (long)(t->left % 60));
    lv_label_set_text(t->value, buf);
    lv_obj_set_width(t->fill, lv_pct(t->total > 0 ? (t->left * 100) / t->total : 0));
}

static void timer_tick(lv_timer_t *tm)
{
    timer_state_t *t = lv_timer_get_user_data(tm);
    if (t->left > 0) {
        t->left--;
        timer_paint(t);
        if (t->left == 0) {
            lv_label_set_text(t->status, "DONE");
        }
    }
}

static void timer_deleted(lv_event_t *e)
{
    timer_state_t *t = lv_event_get_user_data(e);
    lv_timer_delete(t->tick);
    lv_free(t);
}

static lv_obj_t *plate_timer(lv_obj_t *parent, const cJSON *p)
{
    char head[64];
    snprintf(head, sizeof(head), "Timer · %s", jstr(p, "label", "Timer"));
    frame_t f = frame(parent, head, "Live", true);

    timer_state_t *t = lv_malloc_zeroed(sizeof(*t));
    LV_ASSERT_MALLOC(t);
    t->total = (int32_t)jnum(p, "seconds", 60);
    t->left = (int32_t)jnum(p, "remaining", t->total);
    if (t->total < 1) {
        t->total = 1;
    }
    t->value = hero(f.body, "0:00", NULL);
    t->fill = bar(f.body, 8, 100);
    t->status = f.head_right;
    timer_paint(t);
    t->tick = lv_timer_create(timer_tick, 1000, t);
    lv_obj_add_event_cb(f.root, timer_deleted, LV_EVENT_DELETE, t);

    footer(&f, "Hold AI · change", "Tap · stop");
    return f.root;
}

/* ---------- number ---------- */

static lv_obj_t *plate_number(lv_obj_t *parent, const cJSON *p)
{
    char value[32], updated[32];
    value_text(value, sizeof(value), p, "value");
    frame_t f = frame(parent, jstr(p, "label", "Value"), "Now", false);
    hero(f.body, value, jstr(p, "unit", ""));
    sub(f.body, jstr(p, "detail", NULL));
    snprintf(updated, sizeof(updated), "Updated %s", jstr(p, "updated", "now"));
    footer(&f, "Long-press · pin", updated);
    return f.root;
}

/* ---------- answer ---------- */

static lv_obj_t *plate_answer(lv_obj_t *parent, const cJSON *p)
{
    const char *s = jstr(p, "text", "");
    frame_t f = frame(parent, "Answer", jstr(p, "source", ""), false);
    lv_obj_t *l = text(f.body, s, strlen(s) > LONG_ANSWER ? TS_FONT_ROW : TS_FONT_LEAD, TS_INK);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(l, TS_PX(4), 0);
    footer(&f, "Hold AI · follow up", "Swipe · dismiss");
    return f.root;
}

/* ---------- list ---------- */

static lv_obj_t *plate_list(lv_obj_t *parent, const cJSON *p)
{
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(p, "items");
    int n = cJSON_IsArray(items) ? cJSON_GetArraySize(items) : 0;
    int shown = n > LIST_MAX ? LIST_MAX : n;
    int now = (int)jnum(p, "now", -1);
    char count[24];
    snprintf(count, sizeof(count), "%d item%s", n, n == 1 ? "" : "s");
    frame_t f = frame(parent, jstr(p, "title", "List"), count, false);

    for (int i = 0; i < shown; i++) {
        const cJSON *it = cJSON_GetArrayItem(items, i);
        const char *when = "", *what = "";
        if (cJSON_IsArray(it)) {
            const cJSON *a = cJSON_GetArrayItem(it, 0), *b = cJSON_GetArrayItem(it, 1);
            when = cJSON_IsString(a) ? a->valuestring : "";
            what = cJSON_IsString(b) ? b->valuestring : "";
        } else if (cJSON_IsObject(it)) {
            when = jstr(it, "time", "");
            what = jstr(it, "text", "");
        } else if (cJSON_IsString(it)) {
            what = it->valuestring;
        }
        lv_obj_t *r = row(f.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_ver(r, TS_PX(TS_ROW_PAD), 0);
        lv_obj_set_style_pad_column(r, TS_PX(TS_GAP_S), 0);
        rule(r, LV_BORDER_SIDE_BOTTOM);
        lv_obj_t *w = text(r, when, TS_FONT_SUB, i == now ? TS_ACCENT : TS_DIM);
        lv_obj_set_width(w, TS_PX(110));
        lv_obj_t *t = text(r, what, TS_FONT_ROW, TS_INK);
        lv_obj_set_flex_grow(t, 1);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    }
    footer(&f, "Tap · open", n > shown ? "More · swipe" : "Swipe · next");
    return f.root;
}

/* ---------- choice ---------- */

static lv_obj_t *plate_choice(lv_obj_t *parent, const cJSON *p)
{
    const cJSON *opts = cJSON_GetObjectItemCaseSensitive(p, "options");
    int n = cJSON_IsArray(opts) ? cJSON_GetArraySize(opts) : 0;
    if (n > OPTIONS_MAX) {
        n = OPTIONS_MAX;
    }
    int sel = (int)jnum(p, "selected", -1);
    char count[24];
    snprintf(count, sizeof(count), "%d options", n);
    frame_t f = frame(parent, "Choose", count, false);

    /* Options take most of the body, so the question is set in the compact pixel display face and
     * the body is top-aligned instead of centred. */
    lv_obj_set_flex_align(f.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(f.body, TS_PX(TS_GAP_L), 0);
    const char *q = jstr(p, "question", "");
    lv_obj_t *ql = text(f.body, q, strlen(q) > LONG_QUESTION ? TS_FONT_SUB : TS_FONT_DISPLAY, TS_INK);
    lv_obj_set_width(ql, lv_pct(100));
    lv_label_set_long_mode(ql, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_margin_bottom(ql, TS_PX(TS_GAP_L), 0);

    for (int i = 0; i < n; i++) {
        const cJSON *o = cJSON_GetArrayItem(opts, i);
        bool on = (i == sel);
        lv_obj_t *r = row(f.body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_ver(r, TS_PX(16), 0);
        lv_obj_set_style_pad_hor(r, TS_PX(22), 0);
        lv_obj_set_style_border_width(r, TS_PX(TS_RULE_W), 0);
        lv_obj_set_style_border_color(r, on ? TS_ACCENT : TS_RULE, 0);
        lv_obj_set_style_margin_bottom(r, TS_PX(TS_GAP_S), 0);
        if (on) {
            lv_obj_set_style_bg_color(r, TS_ACCENT, 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        }
        text(r, cJSON_IsString(o) ? o->valuestring : "?", TS_FONT_ROW, on ? TS_ON_ACCENT : TS_INK);
        if (on) {
            square(r, 14, TS_ON_ACCENT);
        }
    }
    footer(&f, "Tap · choose", "Hold AI · other");
    return f.root;
}

/* ---------- gauge ---------- */

static lv_obj_t *plate_gauge(lv_obj_t *parent, const cJSON *p)
{
    double min = jnum(p, "min", 0), max = jnum(p, "max", 100), v = jnum(p, "value", 0);
    if (max <= min) {
        max = min + 1;
    }
    char range[32], value[32];
    snprintf(range, sizeof(range), "%g-%g", min, max);
    value_text(value, sizeof(value), p, "value");
    frame_t f = frame(parent, jstr(p, "label", "Gauge"), range, false);
    hero(f.body, value, jstr(p, "unit", ""));

    int on = (int)lround((v - min) / (max - min) * SEGMENTS);
    lv_obj_t *segs = row(f.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(segs, TS_PX(64));
    lv_obj_set_style_pad_column(segs, TS_PX(6), 0);
    lv_obj_set_style_margin_top(segs, TS_PX(TS_GAP_L), 0);
    for (int i = 0; i < SEGMENTS; i++) {
        lv_obj_t *s = box(segs);
        lv_obj_set_height(s, lv_pct(100));
        lv_obj_set_flex_grow(s, 1);
        lv_obj_set_style_bg_color(s, i < on ? TS_ACCENT : TS_RULE, 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    }
    sub(f.body, jstr(p, "detail", NULL));
    footer(&f, "Long-press · pin", "Tap · details");
    return f.root;
}

/* ---------- image ---------- */

static lv_obj_t *plate_image(lv_obj_t *parent, const cJSON *p)
{
    frame_t f = frame(parent, "Photo", jstr(p, "source", "camera"), false);
    lv_obj_set_flex_align(f.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *pic = box(f.body);
    lv_obj_set_size(pic, lv_pct(100), lv_pct(100));
    lv_obj_set_style_margin_ver(pic, TS_PX(20), 0);
    lv_obj_set_style_bg_color(pic, lv_color_hex(0x110F0E), 0);
    lv_obj_set_style_bg_opa(pic, LV_OPA_COVER, 0);
    rule(pic, LV_BORDER_SIDE_FULL);
    /* The real firmware decodes the camera JPEG here with the S31's hardware codec. */
    lv_obj_t *note = label_caps(pic, "Camera frame", TS_FONT_FOOT, TS_DIM);
    lv_obj_center(note);
    footer(&f, jstr(p, "caption", ""), "Hold AI · ask");
    return f.root;
}

/* ---------- dispatch ---------- */

typedef lv_obj_t *(*plate_fn)(lv_obj_t *, const cJSON *);

static const struct {
    const char *type;
    plate_fn build;
} PLATES[] = {
    {"timer", plate_timer}, {"number", plate_number}, {"answer", plate_answer}, {"list", plate_list},
    {"choice", plate_choice}, {"gauge", plate_gauge}, {"image", plate_image},
};

lv_obj_t *ts_plate_create(lv_obj_t *parent, const cJSON *spec)
{
    const char *type = jstr(spec, "type", "");
    for (size_t i = 0; i < sizeof(PLATES) / sizeof(PLATES[0]); i++) {
        if (strcmp(type, PLATES[i].type) == 0) {
            return PLATES[i].build(parent, spec);
        }
    }
    /* Fallback: show whatever text there is, and say the type wasn't understood. */
    char msg[160];
    const char *t = jstr(spec, "text", NULL);
    snprintf(msg, sizeof(msg), "%s", t ? t : (type[0] ? "This plate type isn't supported yet." : "Nothing to show."));
    cJSON *fallback = cJSON_CreateObject();
    cJSON_AddStringToObject(fallback, "text", msg);
    cJSON_AddStringToObject(fallback, "source", type[0] ? type : "unknown");
    lv_obj_t *o = plate_answer(parent, fallback);
    cJSON_Delete(fallback);
    return o;
}

lv_obj_t *ts_plate_create_from_json(lv_obj_t *parent, const char *json)
{
    cJSON *spec = cJSON_Parse(json);
    lv_obj_t *o = ts_plate_create(parent, spec);
    cJSON_Delete(spec);
    return o;
}
