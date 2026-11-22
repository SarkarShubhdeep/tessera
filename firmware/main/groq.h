/* Speech to text through Groq (review 05, decision 009). */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Send 16 kHz mono 16-bit samples to Groq's whisper-large-v3-turbo and copy the words into text.
 * The key goes only into the Authorization header to api.groq.com. *status gets the HTTP status
 * (0 if the connection failed). Blocking: call from the worker task. */
esp_err_t groq_transcribe(const char *key, const int16_t *pcm, size_t samples, char *text, size_t text_len,
                          int *status);
