/* Text-to-speech helpers (review 10) with no ESP-IDF in them, so they can be tested on a Mac. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TTS_MAX_CHARS 200   /* Groq's Orpheus limit per request */

/* Split text into pieces of at most max characters, at sentence ends (. ! ?) where possible, else at a
 * space. Returns the number of pieces written (at most max_pieces); pieces point into buf. */
int tts_split(const char *text, char *buf, size_t buflen, const char **pieces, int max_pieces, size_t max);

typedef struct {
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    const uint8_t *data;
    size_t data_len;
} wav_t;

/* Find "fmt " and "data" in a RIFF/WAVE buffer. A data size of 0 or 0xFFFFFFFF (streamed) means "to the end". */
bool tts_wav_parse(const uint8_t *buf, size_t len, wav_t *out);

/* PCM16 (mono or stereo, any rate) -> 16 kHz mono. malloc'd with alloc (or malloc if NULL); *n_out samples. */
int16_t *tts_to_16k_mono(const wav_t *w, size_t *n_out, void *(*alloc)(size_t));
