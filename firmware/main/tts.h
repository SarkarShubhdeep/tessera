/* Tessera's voice (review 10): Groq text-to-speech into the speaker. */
#pragma once

#include <stdint.h>

/* Start the speech task. After audio_start. */
void tts_start(void);

/* Say text (a copy is taken), replacing anything still being said. released_us is when the AI button was
 * let go, for the timing log (0 if unknown). */
void tts_say(const char *text, int64_t released_us);

/* Stop speaking now (the AI button was pressed). */
void tts_cancel(void);
