/* Claude, text in, plate out (review 04). One question at a time; each is a new conversation. */
#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

/* Start the worker. The key stays in RAM and is sent only to api.anthropic.com. */
esp_err_t claude_start(const char *api_key);

/* Queue a question. Returns false if one is already in flight. */
bool claude_ask(const char *question);

/* For the UI thread: true while a request is in flight. */
bool claude_busy(void);

/* For the UI thread: the next finished plate spec, or NULL. Caller frees with cJSON_Delete. */
cJSON *claude_take_plate(void);
