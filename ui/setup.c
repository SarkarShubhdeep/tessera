/* The Setup plate (review 07). Flat and sharp like the other plates: black, rules, one accent. The QR code is
 * the one light area on the screen, because phone cameras read dark-on-light most reliably. */
#include "setup.h"

#include "lvgl.h"
#include "tokens.h"

#define QR_PX      216   /* the code itself */
#define QR_PAD     12    /* white quiet zone around it */

static lv_obj_t *s_root;
static lv_obj_t *s_title, *s_body, *s_qr_box, *s_qr, *s_line, *s_time, *s_start, *s_cancel;
static ui_setup_cb_t s_cb;
static lv_timer_t *s_close_timer;

void ui_setup_set_handler(ui_setup_cb_t cb) { s_cb = cb; }

bool ui_setup_is_open(void) { return s_root != NULL; }

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    return l;
}

static void on_button(lv_event_t *e)
{
    ui_setup_action_t a = (ui_setup_action_t)(intptr_t)lv_event_get_user_data(e);
    if (s_cb) {
        s_cb(a);
    } else if (a == UI_SETUP_CANCEL) {
        ui_setup_close();
    }
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, bool accent, ui_setup_action_t action)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, TS_PX(150), TS_PX(52));
    lv_obj_set_style_border_width(b, TS_PX(TS_RULE_W), 0);
    lv_obj_set_style_border_color(b, accent ? TS_ACCENT : TS_RULE, 0);
    lv_obj_set_style_bg_color(b, accent ? TS_ACCENT : TS_BLACK, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TS_RULE, LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(b, on_button, LV_EVENT_CLICKED, (void *)(intptr_t)action);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, TS_FONT_HEAD, 0);
    lv_obj_set_style_text_letter_space(l, TS_TRACK_LABEL, 0);
    lv_obj_set_style_text_color(l, accent ? TS_ON_ACCENT : TS_INK, 0);
    lv_obj_center(l);
    return b;
}

static void build(void)
{
    if (s_root) {
        return;
    }
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, TS_BLACK, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);   /* nothing under it gets touches */
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *head = label(s_root, TS_FONT_FOOT, TS_ACCENT);
    lv_obj_set_style_text_letter_space(head, TS_TRACK_LABEL, 0);
    lv_label_set_text(head, "SETUP");
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, TS_PX(34));

    s_title = label(s_root, TS_FONT_SUB, TS_INK);
    lv_obj_set_width(s_title, TS_PX(400));
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, TS_PX(62));

    s_body = label(s_root, TS_FONT_HEAD, TS_DIM);
    lv_obj_set_width(s_body, TS_PX(380));
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_body, TS_PX(6), 0);
    lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, TS_PX(140));

    s_qr_box = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_qr_box);
    lv_obj_set_size(s_qr_box, TS_PX(QR_PX + 2 * QR_PAD), TS_PX(QR_PX + 2 * QR_PAD));
    lv_obj_set_style_bg_color(s_qr_box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_qr_box, LV_OPA_COVER, 0);
    lv_obj_align(s_qr_box, LV_ALIGN_TOP_MID, 0, TS_PX(104));
    s_qr = lv_qrcode_create(s_qr_box);
    lv_qrcode_set_size(s_qr, TS_PX(QR_PX));
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    lv_obj_center(s_qr);

    s_line = label(s_root, TS_FONT_FOOT, TS_DIM);
    lv_obj_set_width(s_line, TS_PX(400));
    lv_label_set_long_mode(s_line, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_line, LV_ALIGN_TOP_MID, 0, TS_PX(358));

    s_start = button(s_root, "START", true, UI_SETUP_START);
    s_cancel = button(s_root, "CANCEL", false, UI_SETUP_CANCEL);

    s_time = label(s_root, TS_FONT_HEAD, TS_DIM);
    lv_obj_set_style_text_font(s_time, TS_FONT_HEAD, 0);
}

static void layout_buttons(bool with_start)
{
    if (with_start) {
        lv_obj_remove_flag(s_start, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_start, LV_ALIGN_BOTTOM_MID, TS_PX(-84), TS_PX(-34));
        lv_obj_align(s_cancel, LV_ALIGN_BOTTOM_MID, TS_PX(84), TS_PX(-34));
    } else {
        lv_obj_add_flag(s_start, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_cancel, LV_ALIGN_BOTTOM_MID, 0, TS_PX(-34));
    }
    lv_obj_remove_flag(s_cancel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(s_time, s_cancel, LV_ALIGN_OUT_RIGHT_MID, TS_PX(18), 0);
}

void ui_setup_open(void)
{
    if (s_close_timer) {
        lv_timer_delete(s_close_timer);
        s_close_timer = NULL;
    }
    build();
    lv_label_set_text(s_title, "Set up from your phone");
    lv_label_set_text(s_body,
                      "Tessera opens a private Wi-Fi hotspot for 5 minutes. Scan its code with your phone, "
                      "then save Wi-Fi and keys on the page it opens.\n\nOnly someone who can see this screen "
                      "can join.");
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_line, "");
    lv_label_set_text(s_time, "");
    layout_buttons(true);
}

void ui_setup_show_qr(const char *title, const char *qr_text, const char *line)
{
    build();
    lv_label_set_text(s_title, title);   /* keep it short: one line above the code */
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    lv_qrcode_update(s_qr, qr_text, (uint32_t)lv_strlen(qr_text));
    lv_label_set_text(s_line, line ? line : "");
    layout_buttons(false);
}

void ui_setup_show_text(const char *title, const char *body)
{
    build();
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_body, body ? body : "");
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    lv_qrcode_update(s_qr, " ", 1);
    lv_label_set_text(s_line, "");
    layout_buttons(false);
}

void ui_setup_set_time(int seconds_left)
{
    if (!s_root) {
        return;
    }
    if (seconds_left < 0) {
        lv_label_set_text(s_time, "");
    } else {
        lv_label_set_text_fmt(s_time, "%d:%02d", seconds_left / 60, seconds_left % 60);
    }
    lv_obj_align_to(s_time, s_cancel, LV_ALIGN_OUT_RIGHT_MID, TS_PX(18), 0);
}

static void close_now(lv_timer_t *t)
{
    (void)t;
    s_close_timer = NULL;
    ui_setup_close();
}

void ui_setup_message(const char *title, const char *line)
{
    build();
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_body, line ? line : "");
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    lv_qrcode_update(s_qr, " ", 1);   /* nothing from the session stays in the drawing */
    lv_label_set_text(s_line, "");
    lv_label_set_text(s_time, "");
    lv_obj_add_flag(s_start, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cancel, LV_OBJ_FLAG_HIDDEN);
    if (s_close_timer) {
        lv_timer_delete(s_close_timer);
    }
    s_close_timer = lv_timer_create(close_now, 2500, NULL);
    lv_timer_set_repeat_count(s_close_timer, 1);
}

void ui_setup_close(void)
{
    if (s_close_timer) {
        lv_timer_delete(s_close_timer);
        s_close_timer = NULL;
    }
    if (s_root) {
        lv_obj_delete(s_root);   /* the QR code and its data go with it */
        s_root = NULL;
    }
}
