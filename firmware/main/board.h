/* ESP-Mosaico v1.2 board layer for the screen-only build.
 *
 * Every GPIO this build drives is set in board.c, and nowhere else. The list, in boot order, is in
 * docs/pin-review-01-screen.md; keep the two in step. */
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include <stdbool.h>

#define BOARD_LCD_W  480
#define BOARD_LCD_H  480

/* Pins this build drives (see the pin review). */
#define BOARD_GPIO_MOTOR     8    /* vibration motor, active high: held low */
#define BOARD_GPIO_PA_EN     45   /* speaker amplifier enable, active high: held low */
#define BOARD_GPIO_VCC_EN    60   /* 3.3 V rail for display and slots, active low: ramped on */
#define BOARD_GPIO_LCD_SCLK  42
#define BOARD_GPIO_LCD_D0    36
#define BOARD_GPIO_LCD_D1    51
#define BOARD_GPIO_LCD_D2    35
#define BOARD_GPIO_LCD_D3    9
#define BOARD_GPIO_LCD_CS    50
#define BOARD_GPIO_LCD_RST   44
/* Pin review 02: inputs and the on-board I2C bus. */
#define BOARD_GPIO_I2C_SDA   56   /* open-drain */
#define BOARD_GPIO_I2C_SCL   3    /* open-drain */
#define BOARD_GPIO_TOUCH_INT 6    /* input, polled */
#define BOARD_GPIO_AI_BTN    7    /* input, pull-up, low = pressed */

/* Ramp the 3.3 V rail on, then bring up the CO5300 panel over QSPI.
 * on_done runs (in an ISR) when a color transfer finishes. */
esp_err_t board_display_init(esp_lcd_panel_io_color_trans_done_cb_t on_done, void *ctx,
                             esp_lcd_panel_handle_t *ret_panel);

/* Pin reviews 03 and 10: I2S to the ES8311 (microphone in, speaker out). */
#define BOARD_GPIO_I2S_MCLK  54
#define BOARD_GPIO_I2S_BCLK  37
#define BOARD_GPIO_I2S_WS    49
#define BOARD_GPIO_I2S_DIN   40   /* input */
#define BOARD_GPIO_I2S_DOUT  52   /* output to the codec's DAC (review 10) */

/* The on-board I2C bus (created by board_touch_init). */
void *board_i2c_bus(void);

/* On-board I2C bus, then the CST9220 touch controller on it (address 0x5A only). */
esp_err_t board_touch_init(esp_lcd_touch_handle_t *ret_touch);

/* AI button: configure as input with pull-up; read returns true while pressed. */
esp_err_t board_ai_button_init(void);
bool board_ai_button_pressed(void);

/* Print the configuration of every pin listed above, for comparison with the pin review. */
void board_dump_pins(void);

/* The speaker amplifier (GPIO45, review 10): the only place it is switched. Low at boot. */
void board_amp(bool on);
bool board_amp_is_on(void);
