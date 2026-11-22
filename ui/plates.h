/* Plates: Tessera's UI units. The agent sends a plate as JSON (the show_plate tool input); the device
 * builds it natively from the design system. Unknown or malformed plates fall back to an answer plate,
 * so a bad spec can never leave the screen blank.
 *
 * Types and fields (see docs/design-system.md):
 *   timer  { label, seconds }
 *   number { value, unit?, label, detail?, updated? }
 *   answer { text, source? }
 *   list   { title, items: [[time, text], ...] (max 5), now? }
 *   choice { question, options: [..] (2-4), selected? }
 *   gauge  { label, value, min?, max?, unit?, detail? }
 *   image  { source, caption? }
 */
#pragma once

#include "cJSON.h"
#include "lvgl.h"

/* Builds a plate filling `parent`. Never returns NULL for a valid parent. */
lv_obj_t *ts_plate_create(lv_obj_t *parent, const cJSON *spec);

/* Same, from a JSON string. */
lv_obj_t *ts_plate_create_from_json(lv_obj_t *parent, const char *json);
