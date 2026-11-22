/* Tessera's own saved keys (review 05). NVS namespace "tessera"; ESP-Claw's "app" stays read-only.
 * nvs_flash_init must have run first (settings_load does it). */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define KEYS_GROQ_MAX 200

/* Copy the saved Groq key into out. False if none is saved. Never creates anything. */
bool keys_get_groq(char *out, size_t len);

/* Length of the saved Groq key, 0 if none. For the log: the key itself is never printed. */
size_t keys_groq_len(void);

/* Starts with gsk_, 20-200 characters, letters, digits, _ and - only. */
bool keys_groq_valid(const char *key);

/* Save (one NVS entry, then commit). ESP_ERR_INVALID_ARG if it doesn't look like a Groq key. */
esp_err_t keys_set_groq(const char *key);

/* Delete that one entry. */
esp_err_t keys_clear_groq(void);

/* Log the settings partition's used/free entry counts (read-only). */
void keys_log_stats(const char *when);
