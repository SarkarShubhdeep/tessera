/*
 * ESP-Mosaico board setup for Tessera (ESP-Claw engine, Board Manager 0.6).
 *
 * Panel and touch factories plus the custom power control for the 3.3 V rail.
 * Adapted from Espressif's esp_boards/esp_mosaico (setup_device.c, board_runtime.c);
 * originals: espressif/esp_boards 0.7.3 on the ESP Component Registry (esp_mosaico).
 *
 * Portions adapted from Espressif code under LicenseRef-Espressif-Modified-MIT; see NOTICE.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_board_device.h"
#include "esp_err.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_log.h"
#include "dev_power_ctrl.h"

static const char *TAG = "esp_mosaico";

/* ---------- Vibration motor (GPIO8, active-high) ----------
 * The motor driver's input floats until something drives GPIO8, and ESP-Claw only initializes the
 * vibration_motor device on demand, so the motor buzzes from boot. Espressif's board fixes this
 * with Board Manager 0.7's init_level; on 0.6 we hold the pin low before app_main runs. */
#define MOSAICO_MOTOR_GPIO  GPIO_NUM_8

__attribute__((constructor)) static void mosaico_motor_off_at_boot(void)
{
    gpio_set_level(MOSAICO_MOTOR_GPIO, 0);
    const gpio_config_t out = {
        .pin_bit_mask = 1ULL << MOSAICO_MOTOR_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&out);
    gpio_set_level(MOSAICO_MOTOR_GPIO, 0);
}

/* ---------- 3.3 V rail (GPIO60, active-low) ----------
 * Espressif ramps the rail on with a 50 ms PWM fade instead of switching it hard, to limit inrush
 * into the panel and module slots. Same approach here. */
#define MOSAICO_VCC_GPIO     GPIO_NUM_60
#define MOSAICO_VCC_RAMP_HZ  100000
#define MOSAICO_VCC_RAMP_MS  50

static bool s_vcc_on;

static esp_err_t vcc_set_steady(bool on)
{
    const gpio_config_t out = {
        .pin_bit_mask = 1ULL << MOSAICO_VCC_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t ret = gpio_set_level(MOSAICO_VCC_GPIO, on ? 0 : 1);
    if (ret == ESP_OK) {
        ret = gpio_config(&out);
    }
    if (ret == ESP_OK) {
        s_vcc_on = on;
    }
    return ret;
}

static esp_err_t vcc_ramp_on(void)
{
    if (s_vcc_on) {
        return ESP_OK;
    }
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = MOSAICO_VCC_RAMP_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = MOSAICO_VCC_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1,
        .duty = 255, /* fully high = rail off (active-low) */
    };
    bool fade_installed_here = false;
    esp_err_t ret = ledc_timer_config(&timer);
    if (ret == ESP_OK) {
        ret = ledc_channel_config(&channel);
    }
    if (ret == ESP_OK) {
        ret = ledc_fade_func_install(0);
        if (ret == ESP_OK) {
            fade_installed_here = true;
        } else if (ret == ESP_ERR_INVALID_STATE) {
            ret = ESP_OK; /* already installed by someone else */
        }
    }
    if (ret == ESP_OK) {
        ret = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0, MOSAICO_VCC_RAMP_MS);
    }
    if (ret == ESP_OK) {
        ret = ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_WAIT_DONE);
    }
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, ret == ESP_OK ? 0 : 1);
    if (fade_installed_here) {
        ledc_fade_func_uninstall();
    }
    ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1);
    const ledc_timer_config_t timer_off = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .deconfigure = true,
    };
    ledc_timer_config(&timer_off);

    /* Hand the pin back to plain GPIO, holding the final level. */
    esp_err_t steady = vcc_set_steady(ret == ESP_OK);
    if (ret == ESP_OK && steady == ESP_OK) {
        ESP_LOGI(TAG, "3.3 V rail on (GPIO%d, %d ms ramp)", MOSAICO_VCC_GPIO, MOSAICO_VCC_RAMP_MS);
    }
    return ret != ESP_OK ? ret : steady;
}

static int vcc_init(const dev_power_ctrl_config_t *config, void **context)
{
    (void)config;
    (void)context;
    esp_err_t ret = vcc_ramp_on();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "3.3 V rail ramp failed: %s", esp_err_to_name(ret));
        return -1;
    }
    return 0;
}

static int vcc_deinit(void *context)
{
    (void)context;
    return vcc_set_steady(false) == ESP_OK ? 0 : -1;
}

static int vcc_set_power(void *context, const char *device_name, bool power_on)
{
    (void)context;
    (void)device_name;
    esp_err_t ret = power_on ? vcc_ramp_on() : vcc_set_steady(false);
    return ret == ESP_OK ? 0 : -1;
}

static const dev_power_ctrl_custom_ops_t s_vcc_ops = {
    .init = vcc_init,
    .deinit = vcc_deinit,
    .set_power = vcc_set_power,
};

DEVICE_EXTRA_FUNC_REGISTER(lcd_power_ctrl, &s_vcc_ops);

/* ---------- CO5300 panel ----------
 * The driver's default init table is for 466x466 modules (x offset 6). The 480x480 table below is
 * the one ESP-Claw uses for Waveshare's 2.16" 480x480 CO5300 panel, with MADCTL (0x36) left at 0
 * so the board manager's swap/mirror settings decide orientation. */
static const co5300_lcd_init_cmd_t s_lcd_init_cmds[] = {
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

static const co5300_vendor_config_t s_lcd_vendor_config = {
    .init_cmds = s_lcd_init_cmds,
    .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
    .flags = {
        .use_qspi_interface = 1,
    },
};

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t cfg = {0};
    memcpy(&cfg, panel_dev_config, sizeof(cfg));
    cfg.vendor_config = (void *)&s_lcd_vendor_config;

    esp_err_t ret = esp_lcd_new_panel_co5300(io, &cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CO5300 panel: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ---------- CST9220 touch ---------- */
esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_touch_config_t *touch_dev_config,
                                    esp_lcd_touch_handle_t *ret_touch)
{
    esp_lcd_touch_config_t cfg = {0};
    memcpy(&cfg, touch_dev_config, sizeof(cfg));

    esp_err_t ret = esp_lcd_touch_new_i2c_cst9220(io, &cfg, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CST9220 touch: %s", esp_err_to_name(ret));
    }
    return ret;
}
