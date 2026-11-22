/* Speech to text through Groq (review 05, decision 009).
 *
 * POST https://api.groq.com/openai/v1/audio/transcriptions, multipart/form-data: the recording as a WAV
 * file, model whisper-large-v3-turbo, response_format json, temperature 0, no language (auto-detect).
 * The body is streamed: form fields and the WAV header first, then the samples straight from PSRAM,
 * then the closing boundary, so the recording is never copied. */
#include "groq.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "keys.h"
#include "mic.h"

static const char *TAG = "groq";

/* api.groq.com sends WE1 -> GTS Root R4, the same chain as api.anthropic.com: same pinned roots. */
extern const char ROOTS_PEM[] asm("_binary_anthropic_roots_pem_start");

#define URL       "https://api.groq.com/openai/v1/audio/transcriptions"
#define MODEL     "whisper-large-v3-turbo"
#define BOUNDARY  "tesseraVoiceBoundary7"
#define RESP_MAX  8192
#define CHUNK     4096

static void wav_header(uint8_t h[44], uint32_t data_bytes)
{
    uint32_t rate = MIC_RATE_HZ, byte_rate = MIC_RATE_HZ * 2, riff = 36 + data_bytes;
    uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
    uint32_t fmt_len = 16;
    memcpy(h, "RIFF", 4);
    memcpy(h + 4, &riff, 4);   /* the ESP32-S31 is little-endian, like WAV */
    memcpy(h + 8, "WAVEfmt ", 8);
    memcpy(h + 16, &fmt_len, 4);
    memcpy(h + 20, &fmt, 2);
    memcpy(h + 22, &ch, 2);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &byte_rate, 4);
    memcpy(h + 32, &align, 2);
    memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_bytes, 4);
}

static bool write_all(esp_http_client_handle_t c, const void *data, int len)
{
    const char *p = data;
    while (len > 0) {
        int n = esp_http_client_write(c, p, len > CHUNK ? CHUNK : len);
        if (n <= 0) {
            return false;
        }
        p += n;
        len -= n;
    }
    return true;
}

esp_err_t groq_transcribe(const char *key, const int16_t *pcm, size_t samples, char *text, size_t text_len,
                          int *status)
{
    text[0] = '\0';
    *status = 0;

    static const char HEAD[] =
        "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" MODEL "\r\n"
        "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"
        "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"temperature\"\r\n\r\n0\r\n"
        "--" BOUNDARY "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    static const char TAIL[] = "\r\n--" BOUNDARY "--\r\n";

    uint32_t data_bytes = (uint32_t)(samples * sizeof(int16_t));
    uint8_t wav[44];
    wav_header(wav, data_bytes);
    int total = (int)(sizeof(HEAD) - 1 + sizeof(wav) + data_bytes + sizeof(TAIL) - 1);

    char auth[16 + KEYS_GROQ_MAX];
    snprintf(auth, sizeof(auth), "Bearer %s", key);

    esp_http_client_config_t cfg = {
        .url = URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .cert_pem = ROOTS_PEM,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        memset(auth, 0, sizeof(auth));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(c, "Content-Type", "multipart/form-data; boundary=" BOUNDARY);
    esp_http_client_set_header(c, "Authorization", auth);
    memset(auth, 0, sizeof(auth));

    char *resp = NULL;
    esp_err_t err = esp_http_client_open(c, total);
    if (err == ESP_OK) {
        bool ok = write_all(c, HEAD, sizeof(HEAD) - 1) && write_all(c, wav, sizeof(wav)) &&
                  write_all(c, pcm, (int)data_bytes) && write_all(c, TAIL, sizeof(TAIL) - 1);
        if (!ok) {
            ESP_LOGE(TAG, "upload interrupted");
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "connection failed: %s", esp_err_to_name(err));
    }
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(c);
        *status = esp_http_client_get_status_code(c);
        resp = heap_caps_calloc(1, RESP_MAX, MALLOC_CAP_SPIRAM);
        int len = 0, n;
        while (resp && len < RESP_MAX - 1 && (n = esp_http_client_read(c, resp + len, RESP_MAX - 1 - len)) > 0) {
            len += n;
        }
        cJSON *r = resp ? cJSON_Parse(resp) : NULL;
        if (*status == 200) {
            const char *t = cJSON_GetStringValue(cJSON_GetObjectItem(r, "text"));
            if (t) {
                while (*t == ' ') {
                    t++;   /* Whisper starts with a space */
                }
                strlcpy(text, t, text_len);
            } else {
                ESP_LOGE(TAG, "HTTP 200 but no text in the reply");
                err = ESP_FAIL;
            }
        } else {
            cJSON *e = cJSON_GetObjectItem(r, "error");
            const char *msg = cJSON_GetStringValue(cJSON_GetObjectItem(e, "message"));
            if (*status == 401 || *status == 403) {
                ESP_LOGE(TAG, "HTTP %d: the saved Groq key was rejected", *status);   /* message not printed */
            } else if (msg && !strstr(msg, "gsk_")) {
                ESP_LOGE(TAG, "HTTP %d: %.120s", *status, msg);
            } else {
                ESP_LOGE(TAG, "HTTP %d", *status);
            }
            err = ESP_FAIL;
        }
        cJSON_Delete(r);
    }
    free(resp);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}
