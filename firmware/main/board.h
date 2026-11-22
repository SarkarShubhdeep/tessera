/* ESP-Mosaico v1.2 board layer for the screen-only build.
 *
 * Every GPIO this build drives is set in board.c, and nowhere else. The list, in boot order, is in
 * docs/pin-review-01-screen.md; keep the two in step. */
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

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

/* Ramp the 3.3 V rail on, then bring up the CO5300 panel over QSPI.
 * on_done runs (in an ISR) when a color transfer finishes. */
esp_err_t board_display_init(esp_lcd_panel_io_color_trans_done_cb_t on_done, void *ctx,
                             esp_lcd_panel_handle_t *ret_panel);

/* Print the configuration of every pin listed above, for comparison with the pin review. */
void board_dump_pins(void);
