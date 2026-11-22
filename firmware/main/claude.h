/* Claude, text in, plate out (review 04). One question at a time; each is a new conversation. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

/* Start the worker. The key stays in RAM and is sent only to api.anthropic.com. */
esp_err_t claude_start(const char *api_key);

typedef enum {
    CLAUDE_EVT_HEARD,   /* text: what Groq heard; Claude is being asked */
    CLAUDE_EVT_PLATE,   /* plate: the answer, or an answer plate explaining an error */
    CLAUDE_EVT_DONE,    /* text: nothing to show beyond this caption (e.g. nothing heard) */
} claude_evt_kind_t;

typedef struct {
    claude_evt_kind_t kind;
    cJSON *plate;       /* caller frees with cJSON_Delete */
    char *text;         /* caller frees with free */
} claude_evt_t;

/* Queue a typed question. Returns false if one is already in flight. */
bool claude_ask(const char *question);

/* Queue a recording (16 kHz mono): Groq turns it into words, then Claude answers. The buffer must stay
 * untouched until claude_busy() is false. Returns false if a request is already in flight. */
bool claude_ask_voice(const int16_t *pcm, size_t samples);

/* For the UI thread: true while a request is in flight. */
bool claude_busy(void);

/* For the UI thread: the next event, if any. */
bool claude_take_event(claude_evt_t *e);
