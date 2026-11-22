/* Tessera's own saved settings (reviews 05 and 06), NVS namespace "tessera". For each setting, Tessera's
 * value wins; otherwise ESP-Claw's saved value (namespace "app", read only) is the fallback.
 * nvs_flash_init must have run first (settings_load does it). */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define KEYS_MAX 300   /* longest value, without the terminator */

typedef enum {
    KEY_WIFI_NAME,
    KEY_WIFI_PASS,
    KEY_CLAUDE,
    KEY_GROQ,
    KEY_COUNT,
} key_id_t;

typedef enum {
    KEY_SRC_NONE,
    KEY_SRC_TESSERA,
    KEY_SRC_ESPCLAW,
} key_src_t;

/* Copy the setting into out (Tessera's, else ESP-Claw's) and say where it came from. Never creates
 * anything. out is empty if the result is KEY_SRC_NONE. */
key_src_t keys_get(key_id_t id, char *out, size_t len);

/* Its length (0 if missing) and source, for the log: values are never printed. */
size_t keys_len(key_id_t id, key_src_t *src);

/* Length limits, prefix (sk-ant-, gsk_) and allowed characters. */
bool keys_valid(key_id_t id, const char *value);

/* Save Tessera's value (one NVS entry, then commit). ESP_ERR_INVALID_ARG if it fails keys_valid. */
esp_err_t keys_set(key_id_t id, const char *value);

/* Delete Tessera's value only; ESP-Claw's fallback, if any, applies again. */
esp_err_t keys_clear(key_id_t id);

/* Log the settings partition's used/free entry counts (read-only). */
void keys_log_stats(const char *when);
