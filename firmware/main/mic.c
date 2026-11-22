/* Microphone level for the ring (pin review 03).
 *
 * I2S0 is receive-only: MCLK 54, BCLK 37, WS 49 out (clocks to the ES8311), DIN 40 in. No DOUT.
 * The ES8311 runs its ADC only; the codec driver gets no amplifier pin, so GPIO45 stays low. */
#include "mic.h"

#include <math.h>

#include "board.h"
#include "driver/i2s_std.h"
#include "es8311_codec.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mic";

#define RATE_HZ     16000
#define FRAME_MS    20
#define FRAMES      (RATE_HZ * FRAME_MS / 1000)
#define GAIN_DB     30.0f
#define FLOOR_DB    (-47.0f)   /* measured: a quiet room reads -49 to -51 dBFS */
#define CEIL_DB     (-20.0f)   /* measured: normal speech peaks around -36 to -19 */

static esp_codec_dev_handle_t s_dev;
static volatile uint8_t s_level;

uint8_t mic_level(void) { return s_level; }

static void mic_task(void *arg)
{
    (void)arg;
    static int16_t buf[FRAMES * 2];   /* stereo frames; the mic is on the left slot */
    float peak_db = -120.0f;
    TickType_t last_log = xTaskGetTickCount();
    while (1) {
        if (esp_codec_dev_read(s_dev, buf, sizeof(buf)) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
            continue;
        }
        double sum = 0;
        for (int i = 0; i < FRAMES; i++) {
            float v = buf[i * 2];
            sum += v * v;
        }
        float rms = sqrtf((float)(sum / FRAMES));
        float db = 20.0f * log10f(rms / 32768.0f + 1e-9f);
        float x = (db - FLOOR_DB) / (CEIL_DB - FLOOR_DB);
        s_level = (uint8_t)(fminf(1.0f, fmaxf(0.0f, x)) * 255.0f);

        if (db > peak_db) {
            peak_db = db;
        }
        if (xTaskGetTickCount() - last_log >= pdMS_TO_TICKS(1000)) {
            ESP_LOGI(TAG, "loudest in the last second: %.1f dBFS", peak_db);
            peak_db = -120.0f;
            last_log = xTaskGetTickCount();
        }
    }
}

esp_err_t mic_start(void)
{
    i2s_chan_handle_t rx;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, NULL, &rx), TAG, "i2s channel");   /* receive only */
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_GPIO_I2S_MCLK,
            .bclk = BOARD_GPIO_I2S_BCLK,
            .ws = BOARD_GPIO_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = BOARD_GPIO_I2S_DIN,
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std), TAG, "i2s std");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = 0x32,                 /* ES8311, 7-bit 0x19 << 1 */
        .bus_handle = board_i2c_bus(),
        .clock_speed_hz = 400000,
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    audio_codec_i2s_cfg_t i2s_cfg = {.port = 0, .rx_handle = rx, .tx_handle = NULL};
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(ctrl && data, ESP_FAIL, TAG, "codec interfaces");

    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl,
        .gpio_if = NULL,              /* no GPIO access at all */
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC,
        .pa_pin = -1,                 /* no amplifier: GPIO45 stays low */
        .use_mclk = true,
        .master_mode = false,
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es);
    ESP_RETURN_ON_FALSE(codec, ESP_FAIL, TAG, "es8311");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec,
        .data_if = data,
    };
    s_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_dev, ESP_FAIL, TAG, "codec dev");
    esp_codec_dev_sample_info_t fs = {.sample_rate = RATE_HZ, .channel = 2, .bits_per_sample = 16};
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_dev, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "open");
    esp_codec_dev_set_in_gain(s_dev, GAIN_DB);

    xTaskCreate(mic_task, "mic", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "ES8311 microphone on, %d Hz, gain %.0f dB", RATE_HZ, GAIN_DB);
    return ESP_OK;
}
