/* The on-board microphone (ES8311 ADC over I2S), reduced to a level for the ring. */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIC_RATE_HZ          16000
#define MIC_REC_MAX_SAMPLES  (MIC_RATE_HZ * 30)   /* 30 s, 960 KB in PSRAM */

/* Start I2S (full duplex), the ES8311 ADC and DAC, the microphone task and the speaker task.
 * Needs the on-board I2C bus (board_touch_init) first. The amplifier stays off until something plays. */
esp_err_t audio_start(void);

/* ---------- speaker (review 10) ---------- */

/* 16 kHz mono 16-bit samples, malloc'd; the speaker frees them. pcm == NULL marks the end of a reply. */
typedef struct {
    int16_t *pcm;
    size_t n;
} spk_chunk_t;

/* Queue a piece of a reply (waits up to wait_ms for room). Chunks that follow within 1.5 s play as one
 * reply. Frees the chunk itself if it can't be queued. */
bool spk_play(spk_chunk_t chunk, int wait_ms);

/* Stop at once and drop anything queued; the amplifier goes off. */
void spk_stop(void);

bool spk_playing(void);
uint8_t spk_level(void);      /* output level 0-255, for the ring */
void spk_set_volume(int pct); /* 0-100 */
int spk_volume(void);

/* Latest level, 0-255 (-47 dBFS = 0, -20 dBFS = 255; tuned on the board). */
uint8_t mic_level(void);

/* Recording for voice (review 05): 16 kHz mono 16-bit into a PSRAM buffer, up to 30 s. RAM only.
 * Don't start a new recording while the previous one is still being sent. */
void mic_record_start(void);

/* Stop; returns the number of samples. *pcm points at the buffer (valid until the next start), *peak_db is
 * the loudest 20 ms frame in dBFS. */
size_t mic_record_stop(const int16_t **pcm, float *peak_db);

/* True once a recording has hit 30 s (it stops growing). */
bool mic_record_full(void);

/* The quiet-room level: a recording never louder than this is treated as silence. */
#define MIC_SILENCE_DB (-47.0f)
