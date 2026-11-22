/* ESP-Mosaico v1.2 board layer: the only file in this build that drives GPIOs.
 *
 * Power and panel code adapted from Espressif's esp_boards/esp_mosaico (board_runtime.c,
 * setup_device.c), espressif/esp_boards 0.7.3 on the ESP Component Registry.
 * Portions adapted from Espressif code under LicenseRef-Espressif-Modified-MIT; see NOTICE.
 * SPDX-License-Identifier: Apache-2.0 */
#include "board.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_log.h"

static const char *TAG = "board";

/* ---------- 1, 2: outputs that must be off before anything else runs ----------
 * A constructor runs during startup, before app_main and before the USB console. The motor's
 * driver input floats until something drives GPIO8; left floating it can switch the motor on, and
 * the motor's current draw resets the board (docs/hardware.md, "the motor at boot"). */
__attribute__((constructor)) static void board_safe_outputs(void)
{
    gpio_set_level(BOARD_GPIO_MOTOR, 0);
    const gpio_config_t motor = {
        .pin_bit_mask = 1ULL << BOARD_GPIO_MOTOR,
        .mode = GPIO_MODE_INPUT_OUTPUT, /* input too, so the pin check can read the level back */
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&motor);
    gpio_set_level(BOARD_GPIO_MOTOR, 0);

    gpio_set_level(BOARD_GPIO_PA_EN, 0);
    const gpio_config_t pa = {
        .pin_bit_mask = 1ULL << BOARD_GPIO_PA_EN,
        .mode = GPIO_MODE_INPUT_OUTPUT,
    };
    gpio_config(&pa);
    gpio_set_level(BOARD_GPIO_PA_EN, 0);
}

/* ---------- 4: the 3.3 V rail (GPIO60, active low) ----------
 * Ramped on with a 50 ms PWM fade, as Espressif's board does, to limit inrush into the panel and the
 * module slots; then handed back to plain GPIO, held low (on). */
#define VCC_RAMP_HZ  100000
#define VCC_RAMP_MS  50

static esp_err_t vcc_ramp_on(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = VCC_RAMP_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = BOARD_GPIO_VCC_EN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1,
        .duty = 255, /* fully high = rail off */
    };
    esp_err_t ret = ledc_timer_config(&timer);
    if (ret == ESP_OK) {
        ret = ledc_channel_config(&channel);
    }
    if (ret == ESP_OK) {
        ret = ledc_fade_func_install(0);
    }
    if (ret == ESP_OK) {
        ret = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0, VCC_RAMP_MS);
    }
    if (ret == ESP_OK) {
        ret = ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_WAIT_DONE);
    }
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, ret == ESP_OK ? 0 : 1);
    ledc_fade_func_uninstall();
    ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1);
    const ledc_timer_config_t timer_off = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .deconfigure = true,
    };
    ledc_timer_config(&timer_off);

    /* Load the output latch before enabling the plain GPIO output. */
    const gpio_config_t out = {
        .pin_bit_mask = 1ULL << BOARD_GPIO_VCC_EN,
        .mode = GPIO_MODE_INPUT_OUTPUT,
    };
    gpio_set_level(BOARD_GPIO_VCC_EN, ret == ESP_OK ? 0 : 1);
    esp_err_t steady = gpio_config(&out);
    ESP_RETURN_ON_ERROR(ret, TAG, "3.3 V rail ramp failed");
    ESP_RETURN_ON_ERROR(steady, TAG, "3.3 V rail hold failed");
    ESP_LOGI(TAG, "3.3 V rail on (GPIO%d, %d ms ramp)", BOARD_GPIO_VCC_EN, VCC_RAMP_MS);
    return ESP_OK;
}

/* ---------- 5-9: the CO5300 panel over QSPI ----------
 * The driver's default init table is for 466 x 466 modules. This 480 x 480 table is the one
 * ESP-Claw's Mosaico build ran on this board, with MADCTL (0x36) at 0. */
static const co5300_lcd_init_cmd_t s_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 600},
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x29, (uint8_t[]){0x00}, 0, 600},
};

#define LCD_PCLK_HZ  (80 * 1000 * 1000)   /* pin review 02; ESP-Claw ran 40 MHz */
#define LCD_MAX_TRANSFER  (BOARD_LCD_W * 80 * 2)

esp_err_t board_display_init(esp_lcd_panel_io_color_trans_done_cb_t on_done, void *ctx,
                             esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_ERROR(vcc_ramp_on(), TAG, "power");

    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(BOARD_GPIO_LCD_SCLK, BOARD_GPIO_LCD_D0,
        BOARD_GPIO_LCD_D1, BOARD_GPIO_LCD_D2, BOARD_GPIO_LCD_D3, LCD_MAX_TRANSFER);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(BOARD_GPIO_LCD_CS, on_done, ctx);
    io_cfg.pclk_hz = LCD_PCLK_HZ;
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io), TAG,
                        "panel io");

    co5300_vendor_config_t vendor = {
        .init_cmds = s_init_cmds,
        .init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]),
        .flags = {.use_qspi_interface = 1},
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_GPIO_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    esp_lcd_panel_handle_t panel;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(io, &panel_cfg, &panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "panel on");
    ESP_LOGI(TAG, "CO5300 %dx%d on, QSPI %d MHz", BOARD_LCD_W, BOARD_LCD_H, LCD_PCLK_HZ / 1000000);
    *ret_panel = panel;
    return ESP_OK;
}

/* ---------- 10-12: on-board I2C and the touch controller ---------- */
static i2c_master_bus_handle_t s_i2c;

void *board_i2c_bus(void) { return s_i2c; }

esp_err_t board_touch_init(esp_lcd_touch_handle_t *ret_touch)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_GPIO_I2C_SDA,
        .scl_io_num = BOARD_GPIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c), TAG, "i2c bus");
    i2c_master_bus_handle_t bus = s_i2c;

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "touch io");

    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_W,
        .y_max = BOARD_LCD_H,
        .rst_gpio_num = GPIO_NUM_NC,              /* no reset line on this board */
        .int_gpio_num = BOARD_GPIO_TOUCH_INT,     /* input only; no callback, so no ISR */
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst9220(io, &cfg, ret_touch), TAG, "touch");
    ESP_LOGI(TAG, "CST9220 touch on I2C0 (SDA %d, SCL %d), INT %d polled", BOARD_GPIO_I2C_SDA,
             BOARD_GPIO_I2C_SCL, BOARD_GPIO_TOUCH_INT);
    return ESP_OK;
}

/* ---------- 13: AI button ---------- */
esp_err_t board_ai_button_init(void)
{
    const gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOARD_GPIO_AI_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&btn);
}

bool board_ai_button_pressed(void) { return gpio_get_level(BOARD_GPIO_AI_BTN) == 0; }

void board_dump_pins(void)
{
    const uint64_t mask = (1ULL << BOARD_GPIO_MOTOR) | (1ULL << BOARD_GPIO_PA_EN) | (1ULL << BOARD_GPIO_VCC_EN) |
                          (1ULL << BOARD_GPIO_LCD_SCLK) | (1ULL << BOARD_GPIO_LCD_D0) | (1ULL << BOARD_GPIO_LCD_D1) |
                          (1ULL << BOARD_GPIO_LCD_D2) | (1ULL << BOARD_GPIO_LCD_D3) | (1ULL << BOARD_GPIO_LCD_CS) |
                          (1ULL << BOARD_GPIO_LCD_RST) | (1ULL << BOARD_GPIO_I2C_SDA) | (1ULL << BOARD_GPIO_I2C_SCL) |
                          (1ULL << BOARD_GPIO_TOUCH_INT) | (1ULL << BOARD_GPIO_AI_BTN) | (1ULL << BOARD_GPIO_I2S_MCLK) |
                          (1ULL << BOARD_GPIO_I2S_BCLK) | (1ULL << BOARD_GPIO_I2S_WS) | (1ULL << BOARD_GPIO_I2S_DIN) |
                          (1ULL << 52) | (1ULL << 57) | (1ULL << 34);
    printf("--- pin check (compare with docs/pin-review-01-screen.md and -02-input.md, -03-mic.md) ---\n");
    gpio_dump_io_configuration(stdout, mask);
    ESP_LOGI(TAG, "levels: motor(8)=%d (want 0), pa(45)=%d (want 0), vcc_en(60)=%d (want 0 = rail on)",
             gpio_get_level(BOARD_GPIO_MOTOR), gpio_get_level(BOARD_GPIO_PA_EN), gpio_get_level(BOARD_GPIO_VCC_EN));
}
