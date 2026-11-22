/* Audio (pin reviews 03 and 10): the microphone in, the speaker out.
 *
 * I2S0 is full duplex at 16 kHz: MCLK 54, BCLK 37, WS 49 out (clocks to the ES8311), DIN 40 in, DOUT 52 out.
 * The ES8311 runs its ADC (microphone) and DAC (speaker). The codec driver gets no amplifier pin and no GPIO
 * access: the amplifier (GPIO45) is switched only by board_amp(), from the speaker task here, on only while a
 * reply plays, with a 10 s watchdog that turns it off if playback stalls. */
#include "audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "driver/i2s_std.h"
#include "es8311_codec.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "mic";

#define RATE_HZ     MIC_RATE_HZ
#define FRAME_MS    20
#define FRAMES      (RATE_HZ * FRAME_MS / 1000)
#define GAIN_DB     30.0f
#define FLOOR_DB    (-47.0f)   /* measured: a quiet room reads -49 to -51 dBFS */
#define CEIL_DB     (-20.0f)   /* measured: normal speech peaks around -36 to -19 */

static esp_codec_dev_handle_t s_dev;       /* microphone */
static esp_codec_dev_handle_t s_out;       /* speaker */
static volatile uint8_t s_level;

/* Recording (review 05): mono 16 kHz into one PSRAM buffer, allocated once. The mic task appends a
 * frame, then advances s_rec_len, so a reader that takes s_rec_len after stopping sees whole frames. */
static int16_t *s_rec;
static volatile bool s_recording;
static volatile size_t s_rec_len;
static volatile float s_rec_peak_db;

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

        if (s_recording && s_rec) {
            size_t n = s_rec_len;
            size_t room = MIC_REC_MAX_SAMPLES - n;
            size_t take = room < FRAMES ? room : FRAMES;
            for (size_t i = 0; i < take; i++) {
                s_rec[n + i] = buf[i * 2];   /* left slot */
            }
            s_rec_len = n + take;
            if (db > s_rec_peak_db) {
                s_rec_peak_db = db;
            }
        }

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

/* ---------- speaker (review 10) ---------- */

#define SPK_QUEUE      3
#define WATCHDOG_US    (10 * 1000 * 1000)   /* amplifier off if no audio was written for this long */
#define LEAD_MS        30                   /* silence before switching the amplifier on, and after it goes off */

static QueueHandle_t s_spk_q;
static volatile bool s_abort;
static volatile bool s_playing;
static volatile uint8_t s_out_level;
static int s_volume = 60;
static esp_timer_handle_t s_wd;

uint8_t spk_level(void) { return s_out_level; }
bool spk_playing(void) { return s_playing; }

static void watchdog(void *arg)
{
    (void)arg;
    board_amp(false);   /* playback stalled: never leave the amplifier on */
    s_abort = true;
    ESP_LOGW(TAG, "speaker watchdog: amplifier off");
}

static void free_chunk(spk_chunk_t *c)
{
    free(c->pcm);
    c->pcm = NULL;
}

void spk_stop(void)
{
    s_abort = true;
    spk_chunk_t c;
    while (s_spk_q && xQueueReceive(s_spk_q, &c, 0) == pdTRUE) {
        free_chunk(&c);
    }
}

bool spk_play(spk_chunk_t chunk, int wait_ms)
{
    if (!s_spk_q || xQueueSend(s_spk_q, &chunk, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        free_chunk(&chunk);
        return false;
    }
    return true;
}

void spk_set_volume(int pct)
{
    s_volume = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    if (s_out) {
        esp_codec_dev_set_out_vol(s_out, s_volume);
    }
}

int spk_volume(void) { return s_volume; }

/* Mono 16 kHz in, stereo frames out (the codec takes both slots), 20 ms at a time. */
static void write_silence(int ms)
{
    static int16_t z[FRAMES * 2];
    memset(z, 0, sizeof(z));
    for (int t = 0; t < ms; t += FRAME_MS) {
        esp_codec_dev_write(s_out, z, sizeof(z));
    }
}

static void spk_task(void *arg)
{
    (void)arg;
    static int16_t frame[FRAMES * 2];
    while (1) {
        spk_chunk_t c;
        xQueueReceive(s_spk_q, &c, portMAX_DELAY);
        if (!c.pcm) {
            continue;   /* an end marker with nothing playing */
        }
        /* An utterance starts: silence first, then the amplifier, so it doesn't pop. */
        s_abort = false;
        s_playing = true;
        write_silence(LEAD_MS);
        board_amp(true);
        esp_timer_start_once(s_wd, WATCHDOG_US);
        int64_t t0 = esp_timer_get_time();
        size_t played = 0;
        bool more = true;
        while (more && !s_abort) {
            for (size_t i = 0; i < c.n && !s_abort; i += FRAMES) {
                size_t n = c.n - i < FRAMES ? c.n - i : FRAMES;
                double sum = 0;
                for (size_t k = 0; k < FRAMES; k++) {
                    int16_t v = k < n ? c.pcm[i + k] : 0;
                    frame[k * 2] = v;
                    frame[k * 2 + 1] = v;
                    sum += (double)v * v;
                }
                esp_codec_dev_write(s_out, frame, sizeof(frame));
                esp_timer_restart(s_wd, WATCHDOG_US);
                float db = 20.0f * log10f(sqrtf((float)(sum / FRAMES)) / 32768.0f + 1e-9f);
                float x = (db + 50.0f) / 40.0f;   /* -50..-10 dBFS -> 0..1 */
                s_out_level = (uint8_t)(fminf(1.0f, fmaxf(0.0f, x)) * 255.0f);
                played += n;
            }
            free_chunk(&c);
            /* The next piece of the same reply, if it arrives soon; an end marker or a pause ends it. */
            more = xQueueReceive(s_spk_q, &c, pdMS_TO_TICKS(1500)) == pdTRUE && c.pcm != NULL;
        }
        if (c.pcm) {
            free_chunk(&c);
        }
        board_amp(false);   /* off first, then silence, so the end doesn't pop either */
        esp_timer_stop(s_wd);
        write_silence(LEAD_MS);
        s_out_level = 0;
        s_playing = false;
        ESP_LOGI(TAG, "speaker: played %u ms of audio in %d ms%s", (unsigned)(played * 1000 / RATE_HZ),
                 (int)((esp_timer_get_time() - t0) / 1000), s_abort ? " (stopped)" : "");
        if (s_abort) {
            spk_stop();   /* drop anything still queued */
        }
    }
}

static esp_err_t spk_start(void)
{
    s_spk_q = xQueueCreate(SPK_QUEUE, sizeof(spk_chunk_t));
    const esp_timer_create_args_t wd = {.callback = watchdog, .name = "amp_wd"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&wd, &s_wd), TAG, "watchdog");
    return xTaskCreate(spk_task, "speaker", 4096, NULL, 6, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_start(void)
{
    i2s_chan_handle_t rx, tx;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear_before_cb = true;   /* the speaker side sends zeros (silence) whenever nothing is queued */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &tx, &rx), TAG, "i2s channels");   /* full duplex (review 10) */
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_GPIO_I2S_MCLK,
            .bclk = BOARD_GPIO_I2S_BCLK,
            .ws = BOARD_GPIO_I2S_WS,
            .dout = BOARD_GPIO_I2S_DOUT,
            .din = BOARD_GPIO_I2S_DIN,
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std), TAG, "i2s std tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std), TAG, "i2s std rx");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = 0x32,                 /* ES8311, 7-bit 0x19 << 1 */
        .bus_handle = board_i2c_bus(),
        .clock_speed_hz = 400000,
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    audio_codec_i2s_cfg_t i2s_cfg = {.port = 0, .rx_handle = rx, .tx_handle = tx};
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(ctrl && data, ESP_FAIL, TAG, "codec interfaces");

    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl,
        .gpio_if = NULL,              /* no GPIO access at all */
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,   /* ADC and DAC (review 10) */
        .pa_pin = -1,                 /* the driver never touches GPIO45: board_amp() does */
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

    esp_codec_dev_cfg_t out_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec,
        .data_if = data,
    };
    s_out = esp_codec_dev_new(&out_cfg);
    ESP_RETURN_ON_FALSE(s_out, ESP_FAIL, TAG, "codec out dev");
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_out, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "open out");
    esp_codec_dev_set_out_vol(s_out, s_volume);
    ESP_RETURN_ON_ERROR(spk_start(), TAG, "speaker task");

    s_rec = heap_caps_malloc(MIC_REC_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_rec, ESP_ERR_NO_MEM, TAG, "recording buffer");

    xTaskCreate(mic_task, "mic", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "ES8311 on: microphone %d Hz, gain %.0f dB; speaker volume %d%%", RATE_HZ, GAIN_DB, s_volume);
    return ESP_OK;
}

void mic_record_start(void)
{
    s_recording = false;
    s_rec_len = 0;
    s_rec_peak_db = -120.0f;
    s_recording = true;
}

size_t mic_record_stop(const int16_t **pcm, float *peak_db)
{
    s_recording = false;
    if (pcm) {
        *pcm = s_rec;
    }
    if (peak_db) {
        *peak_db = s_rec_peak_db;
    }
    return s_rec_len;
}

bool mic_record_full(void) { return s_recording && s_rec_len >= MIC_REC_MAX_SAMPLES; }
