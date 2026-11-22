/* Tessera surface: the presence ring and a deck of plates. Device-independent: the same files compile for the
 * Mac simulator and the board. The platform layer creates the display and input first. */
#pragma once

#include "cJSON.h"
#include "lvgl.h"

typedef enum {
    UI_STATE_IDLE,
    UI_STATE_LISTENING,
    UI_STATE_THINKING,
    UI_STATE_SPEAKING,
    UI_STATE_COUNT,
} ui_state_t;

/* Build the surface on the active screen, showing the orb. */
void ui_create(void);

/* Orb state (the engine's `state` event). Shows the orb if a plate is up. */
void ui_set_state(ui_state_t state);

/* Current ring state. */
ui_state_t ui_get_state(void);

/* Live audio level, 0-255: the microphone while listening, the speaker while speaking. The engine
 * calls this ~30 times a second; the ring's segment lengths follow it. */
void ui_set_level(uint8_t level);

/* Show a plate (the engine's `plate` event). It becomes the newest plate in the deck. */
void ui_show_plate(const cJSON *spec, bool animate);

/* Swipe through the deck: +1 = next (newer), -1 = previous. Position -1 is the orb. */
void ui_swipe(int dir);

/* Number of plates in the deck, and the current position (-1 = orb). */
int ui_deck_size(void);
int ui_deck_pos(void);
