/* Settings saved by the factory ESP-Claw firmware, read on the device (review 04). Read-only. */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char api_key[320];
    char model[64];        /* ESP-Claw's saved model; logged, not used */
    char backend[32];
} tsr_settings_t;

/* Read the settings. Never writes or erases NVS. Logs presence and lengths only, never values. */
esp_err_t settings_load(tsr_settings_t *out);

/* True if the saved key is a Claude API key. */
bool settings_key_is_claude(const tsr_settings_t *s);
