/* Mosaico UI: device-independent screens. The same files compile for the Mac simulator and the board,
 * so nothing here may touch hardware; the platform layer creates the display and input first. */
#pragma once

#include "lvgl.h"

typedef enum {
    UI_STATE_IDLE,
    UI_STATE_LISTENING,
    UI_STATE_THINKING,
    UI_STATE_SPEAKING,
    UI_STATE_COUNT,
} ui_state_t;

/* Build the home screen on the active display. */
void ui_create(void);

/* Switch the agent state shown on screen (the firmware will call this from the agent events). */
void ui_set_state(ui_state_t state);
