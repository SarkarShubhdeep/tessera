/* The Setup plate (review 07): set up Wi-Fi and keys from a phone through a private hotspot.
 * Device-independent like the rest of ui/: the firmware drives it from the setup task's state; the simulator
 * shows it with made-up codes. Drawn on the top layer, over the ring and plates. */
#pragma once

#include <stdbool.h>

typedef enum {
    UI_SETUP_START,    /* the owner chose Start on the intro */
    UI_SETUP_CANCEL,   /* Cancel, at any stage */
} ui_setup_action_t;

typedef void (*ui_setup_cb_t)(ui_setup_action_t action);

/* Who hears Start and Cancel. */
void ui_setup_set_handler(ui_setup_cb_t cb);

/* The intro: what setup does, with Start and Cancel. Opened by a 10 s hold on the ring. */
void ui_setup_open(void);

/* A step with a QR code: a title above, one line under it. */
void ui_setup_show_qr(const char *title, const char *qr_text, const char *line);

/* A step with text instead of a code, still with Cancel. */
void ui_setup_show_text(const char *title, const char *body);

/* Time left, shown next to Cancel. Negative hides it. */
void ui_setup_set_time(int seconds_left);

/* A last message (no QR, no buttons), then the plate closes by itself after 2.5 s. */
void ui_setup_message(const char *title, const char *line);

/* Close now, without a message. */
void ui_setup_close(void);

bool ui_setup_is_open(void);
