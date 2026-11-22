/* The on-board microphone (ES8311 ADC over I2S), reduced to a level for the ring. */
#pragma once

#include "esp_err.h"
#include <stdint.h>

/* Start I2S (receive only) and the ES8311 ADC, and a task that measures the level every 20 ms.
 * Needs the on-board I2C bus (board_touch_init) first. */
esp_err_t mic_start(void);

/* Latest level, 0-255 (about -60 dBFS = 0, -15 dBFS = 255). */
uint8_t mic_level(void);
