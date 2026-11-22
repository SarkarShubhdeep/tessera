/* Tessera's voice (review 10).
 *
 * POST https://api.groq.com/openai/v1/audio/speech, model canopylabs/orpheus-v1-english, a WAV back. At most 200
 * characters per request, so longer text is split at sentence ends and the next piece is fetched while the
 * current one plays. The WAV is resampled to 16 kHz mono for the speaker. Same Groq key and pinned Google
 * Trust Services roots as speech-to-text. The spoken text is never logged. */
#include "tts.h"

#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "keys.h"
#include "net.h"
#include "tts_parse.h"

static const char *TAG = "speech";

extern const char ROOTS_PEM[] asm("_binary_anthropic_roots_pem_start");

#define URL       "https://api.groq.com/openai/v1/audio/speech"
#define MODEL     "canopylabs/orpheus-v1-english"
#define VOICE     "hannah"
#define WAV_MAX   (2 * 1024 * 1024)   /* 200 characters is about 12 s; at 48 kHz stereo that's 2.3 MB worst case */

typedef struct {
    char *text;
    int64_t released_us;
    uint32_t gen;
} say_t;

static QueueHandle_t s_q;
static volatile uint32_t s_gen;   /* bumped by tts_cancel/tts_say: older work stops */

typedef struct {
    uint8_t *buf;
    size_t len;
    bool full;
} wavbuf_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    wavbuf_t *b = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        if (b->len + e->data_len > WAV_MAX) {
            b->full = true;
        } else {
            memcpy(b->buf + b->len, e->data, e->data_len);
            b->len += e->data_len;
        }
    }
    return ESP_OK;
}

static void *psram(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }

/* One request: text -> 16 kHz mono samples (malloc'd), or NULL. */
static int16_t *speak_piece(const char *piece, const char *key, wavbuf_t *wb, size_t *n_out, int *status)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", MODEL);
    cJSON_AddStringToObject(req, "voice", VOICE);
    cJSON_AddStringToObject(req, "input", piece);
    cJSON_AddStringToObject(req, "response_format", "wav");
    char *json = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    char auth[16 + KEYS_MAX];
    snprintf(auth, sizeof(auth), "Bearer %s", key);
    esp_http_client_config_t cfg = {
        .url = URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .cert_pem = ROOTS_PEM,
        .event_handler = on_http,
        .user_data = wb,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    wb->len = 0;
    wb->full = false;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "Authorization", auth);
    memset(auth, 0, sizeof(auth));
    esp_http_client_set_post_field(c, json, strlen(json));
    esp_err_t err = esp_http_client_perform(c);
    *status = err == ESP_OK ? esp_http_client_get_status_code(c) : 0;
    esp_http_client_cleanup(c);
    memset(json, 0, strlen(json));   /* the spoken text */
    cJSON_free(json);

    if (*status != 200 || wb->full) {
        return NULL;
    }
    wav_t w;
    if (!tts_wav_parse(wb->buf, wb->len, &w)) {
        ESP_LOGE(TAG, "the reply wasn't a PCM WAV (%u bytes)", (unsigned)wb->len);
        return NULL;
    }
    static bool told;
    if (!told) {
        ESP_LOGI(TAG, "Groq WAV: %u Hz, %u channel(s), %u-bit", (unsigned)w.rate, w.channels, w.bits);
        told = true;
    }
    return tts_to_16k_mono(&w, n_out, psram);
}

static void tts_task(void *arg)
{
    (void)arg;
    wavbuf_t wb = {.buf = psram(WAV_MAX)};
    static char split_buf[1024];
    while (1) {
        say_t s;
        xQueueReceive(s_q, &s, portMAX_DELAY);
        if (s.gen != s_gen || !wb.buf) {
            free(s.text);
            continue;
        }
        char key[KEYS_MAX + 1];
        if (!net_is_up() || keys_get(KEY_GROQ, key, sizeof(key)) == KEY_SRC_NONE) {
            ESP_LOGW(TAG, "not spoken: %s", net_is_up() ? "no Groq key" : "no Wi-Fi");
            free(s.text);
            continue;
        }
        const char *pieces[8];
        int n = tts_split(s.text, split_buf, sizeof(split_buf), pieces, 8, TTS_MAX_CHARS);
        int64_t t0 = esp_timer_get_time();
        int64_t first_audio_us = 0;
        int ms_tts = 0;
        for (int i = 0; i < n && s.gen == s_gen; i++) {
            int status = 0;
            size_t samples = 0;
            int64_t tp = esp_timer_get_time();
            int16_t *pcm = speak_piece(pieces[i], key, &wb, &samples, &status);
            ms_tts += (int)((esp_timer_get_time() - tp) / 1000);
            if (!pcm) {
                ESP_LOGE(TAG, "piece %d/%d: HTTP %d%s", i + 1, n, status,
                         status == 429 ? " (Groq limit reached)" : status == 401 ? " (key rejected)" : "");
                break;
            }
            if (s.gen != s_gen) {
                free(pcm);
                break;
            }
            if (!first_audio_us) {
                first_audio_us = esp_timer_get_time();
            }
            spk_play((spk_chunk_t){.pcm = pcm, .n = samples}, 15000);
        }
        spk_play((spk_chunk_t){0}, 1000);   /* end of this reply */
        memset(key, 0, sizeof(key));
        memset(split_buf, 0, sizeof(split_buf));
        memset(s.text, 0, strlen(s.text));
        free(s.text);
        if (first_audio_us) {
            ESP_LOGI(TAG, "%d piece(s): text to speech %d ms; first sound %d ms after the plate%s", n, ms_tts,
                     (int)((first_audio_us - t0) / 1000), "");
            if (s.released_us) {
                ESP_LOGI(TAG, "release to first sound %d ms", (int)((first_audio_us - s.released_us) / 1000));
            }
        }
    }
}

void tts_start(void)
{
    s_q = xQueueCreate(2, sizeof(say_t));
    xTaskCreate(tts_task, "tts", 8192, NULL, 4, NULL);
}

void tts_say(const char *text, int64_t released_us)
{
    if (!s_q || !text || !*text || spk_volume() == 0) {
        return;
    }
    tts_cancel();
    say_t s = {.text = strdup(text), .released_us = released_us, .gen = s_gen};
    if (!s.text || xQueueSend(s_q, &s, 0) != pdTRUE) {
        free(s.text);
    }
}

void tts_cancel(void)
{
    s_gen++;
    spk_stop();
}
