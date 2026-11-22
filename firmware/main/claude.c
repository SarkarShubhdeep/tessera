/* The Claude Messages API over raw HTTPS (C has no official SDK).
 *
 * POST https://api.anthropic.com/v1/messages, anthropic-version 2026-10-21, model claude-opus-5-5 at
 * effort low, server-side fallback on. One tool, show_plate: the agent answers by choosing a plate from
 * the design system and filling it in; the device draws it (decision 005). */
#include "claude.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "groq.h"
#include "keys.h"
#include "net.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "claude";

/* Trust anchors: Google Trust Services roots (certs/anthropic_roots.pem). ESP-IDF's bundle check fails
 * here: the server sends GTS Root R4 cross-signed by GlobalSign Root CA, which the bundle has dropped. */
extern const char ROOTS_PEM[] asm("_binary_anthropic_roots_pem_start");

#define URL        "https://api.anthropic.com/v1/messages"
#define MODEL      "claude-opus-5-5"
#define BODY_MAX   (64 * 1024)

static const char *SYSTEM_PROMPT =
    "You are Tessera, the assistant on a small square 480x480 screen with one button. "
    "Every answer is shown as a plate: call show_plate exactly once, choosing the plate type that fits "
    "and filling in its fields. Do not answer with plain text.\n"
    "Plates and their fields:\n"
    "- number: value (number or short string), unit, label, detail, updated. For one figure.\n"
    "- answer: text (at most about 120 characters; shorter is better), source. For a short answer.\n"
    "- list: title, items (up to 5 pairs of [time or tag of at most 5 characters, text of at most 16 "
    "characters]), now (index of the current item or -1). Each item is one short line.\n"
    "- choice: question, options (2 to 4 short strings), selected (-1 if none). When the user must pick.\n"
    "- gauge: label, value, min, max, unit, detail. For a value in a known range.\n"
    "- timer: label, seconds. When asked to time something.\n"
    "Keep every string short: the screen is small and the type is large. No markdown.";

static const char *TOOL_JSON =
    "{\"name\":\"show_plate\","
    "\"description\":\"Show the answer on the device's screen as one plate. type picks the plate; fill in "
    "only the fields that plate uses.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"type\":{\"type\":\"string\",\"enum\":[\"number\",\"answer\",\"list\",\"choice\",\"gauge\",\"timer\"]},"
    "\"label\":{\"type\":\"string\"},\"value\":{\"type\":[\"number\",\"string\"]},\"unit\":{\"type\":\"string\"},"
    "\"detail\":{\"type\":\"string\"},\"updated\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"},"
    "\"source\":{\"type\":\"string\"},\"title\":{\"type\":\"string\"},"
    "\"items\":{\"type\":\"array\",\"items\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},"
    "\"now\":{\"type\":\"integer\"},\"question\":{\"type\":\"string\"},"
    "\"options\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"selected\":{\"type\":\"integer\"},"
    "\"min\":{\"type\":\"number\"},\"max\":{\"type\":\"number\"},\"seconds\":{\"type\":\"integer\"}},"
    "\"required\":[\"type\"]}}";

typedef struct {
    char *question;          /* unused since issue #16: questions come from the AI button only */
    const int16_t *pcm;      /* the recording (mic's PSRAM buffer) */
    size_t samples;
} job_t;

static QueueHandle_t s_in;      /* job_t */
static QueueHandle_t s_out;     /* claude_evt_t */
static volatile bool s_busy;

typedef struct {
    char *buf;
    int len;
} body_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    body_t *b = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && b->len + e->data_len < BODY_MAX) {
        memcpy(b->buf + b->len, e->data, e->data_len);
        b->len += e->data_len;
        b->buf[b->len] = '\0';
    }
    return ESP_OK;
}

static cJSON *answer_plate(const char *text, const char *label)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "answer");
    cJSON_AddStringToObject(p, "text", text);
    if (label) {
        cJSON_AddStringToObject(p, "source", label);
    }
    return p;
}

static char *build_request(const char *question)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", MODEL);
    cJSON_AddNumberToObject(req, "max_tokens", 16000);
    cJSON *oc = cJSON_AddObjectToObject(req, "output_config");
    cJSON_AddStringToObject(oc, "effort", "low");
    cJSON_AddStringToObject(req, "fallbacks", "default");
    cJSON_AddStringToObject(req, "system", SYSTEM_PROMPT);
    cJSON *tools = cJSON_AddArrayToObject(req, "tools");
    cJSON_AddItemToArray(tools, cJSON_Parse(TOOL_JSON));
    cJSON *tc = cJSON_AddObjectToObject(req, "tool_choice");
    cJSON_AddStringToObject(tc, "type", "auto");
    cJSON *msgs = cJSON_AddArrayToObject(req, "messages");
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "user");
    cJSON_AddStringToObject(m, "content", question);
    cJSON_AddItemToArray(msgs, m);
    char *s = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    return s;
}

/* Turn a Messages API response into a plate spec. */
static cJSON *plate_from_response(int status, const char *body)
{
    cJSON *r = cJSON_Parse(body);
    if (!r) {
        ESP_LOGE(TAG, "HTTP %d, unreadable response", status);
        return answer_plate("No answer: the reply couldn't be read.", "Error");
    }
    cJSON *plate = NULL;
    if (status != 200) {
        cJSON *err = cJSON_GetObjectItem(r, "error");
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(err, "type"));
        const char *msg = cJSON_GetStringValue(cJSON_GetObjectItem(err, "message"));
        ESP_LOGE(TAG, "HTTP %d %s: %s", status, type ? type : "?", msg ? msg : "?");
        char t[96];
        snprintf(t, sizeof(t), "Claude returned an error (%d%s%s).", status, type ? ", " : "", type ? type : "");
        plate = answer_plate(t, "Error");
    } else {
        const char *stop = cJSON_GetStringValue(cJSON_GetObjectItem(r, "stop_reason"));
        cJSON *usage = cJSON_GetObjectItem(r, "usage");
        ESP_LOGI(TAG, "stop %s, tokens in %d / out %d", stop ? stop : "?",
                 (int)cJSON_GetNumberValue(cJSON_GetObjectItem(usage, "input_tokens")),
                 (int)cJSON_GetNumberValue(cJSON_GetObjectItem(usage, "output_tokens")));
        if (stop && strcmp(stop, "refusal") == 0) {
            plate = answer_plate("Claude declined that request.", "Claude");
        } else {
            const char *text = NULL;
            cJSON *block;
            cJSON_ArrayForEach(block, cJSON_GetObjectItem(r, "content")) {
                const char *bt = cJSON_GetStringValue(cJSON_GetObjectItem(block, "type"));
                const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(block, "name"));
                if (bt && strcmp(bt, "tool_use") == 0 && name && strcmp(name, "show_plate") == 0 && !plate) {
                    plate = cJSON_Duplicate(cJSON_GetObjectItem(block, "input"), true);
                } else if (bt && strcmp(bt, "text") == 0 && !text) {
                    text = cJSON_GetStringValue(cJSON_GetObjectItem(block, "text"));
                }
            }
            if (!plate) {
                plate = answer_plate(text && text[0] ? text : "No answer came back.", "Claude");
            }
            char *spec = cJSON_PrintUnformatted(plate);   /* the plate content only: no secrets */
            if (spec) {
                ESP_LOGI(TAG, "plate: %s", spec);
                cJSON_free(spec);
            }
        }
    }
    cJSON_Delete(r);
    return plate;
}

static cJSON *ask_claude(const char *q, body_t *body)
{
    /* Read for each question (review 06): Tessera's saved key, else ESP-Claw's. Not kept in RAM. */
    char key[KEYS_MAX + 1];
    if (keys_get(KEY_CLAUDE, key, sizeof(key)) == KEY_SRC_NONE) {
        return answer_plate("No Claude key saved. Type key claude and the key into the console.", "Setup");
    }
    if (!net_is_up()) {
        memset(key, 0, sizeof(key));
        return answer_plate("No Wi-Fi, so nothing was sent.", "Error");
    }
    ESP_LOGI(TAG, "asking: %s", q);
    char *req = build_request(q);
    esp_http_client_config_t cfg = {
        .url = URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 90000,
        .cert_pem = ROOTS_PEM,
        .event_handler = on_http,
        .user_data = body,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    body->len = 0;
    body->buf[0] = '\0';
    int64_t t0 = esp_timer_get_time();
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "content-type", "application/json");
    esp_http_client_set_header(c, "x-api-key", key);   /* copied by the client; ours is wiped next */
    memset(key, 0, sizeof(key));
    esp_http_client_set_header(c, "anthropic-version", "2026-10-21");
    esp_http_client_set_header(c, "anthropic-beta", "server-side-fallback-2026-07-01");
    esp_http_client_set_post_field(c, req, strlen(req));
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    free(req);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "request failed: %s", esp_err_to_name(err));
        return answer_plate("No connection to Claude.", "Error");
    }
    ESP_LOGI(TAG, "reply in %d ms (HTTP %d, %d bytes)", (int)((esp_timer_get_time() - t0) / 1000), status,
             body->len);
    return plate_from_response(status, body->buf);
}

static void emit(claude_evt_kind_t kind, cJSON *plate, const char *text)
{
    claude_evt_t e = {.kind = kind, .plate = plate, .text = text ? strdup(text) : NULL};
    xQueueSend(s_out, &e, portMAX_DELAY);
}

/* A recording: Groq for the words, then Claude for the plate. Returns the question (malloc'd) or NULL. */
static char *transcribe(const job_t *job)
{
    int64_t t0 = esp_timer_get_time();
    if (!net_is_up()) {
        emit(CLAUDE_EVT_PLATE, answer_plate("No Wi-Fi, so nothing was sent.", "Error"), NULL);
        return NULL;
    }
    if (keys_len(KEY_CLAUDE, NULL) == 0) {
        /* Don't spend a Groq request on words Claude can't answer. */
        emit(CLAUDE_EVT_PLATE,
             answer_plate("No Claude key saved. Type key claude and the key into the console.", "Setup"), NULL);
        return NULL;
    }
    char key[KEYS_MAX + 1];
    if (keys_get(KEY_GROQ, key, sizeof(key)) == KEY_SRC_NONE) {
        emit(CLAUDE_EVT_PLATE,
             answer_plate("No Groq key saved. Type key groq and the key into the console.", "Setup"), NULL);
        return NULL;
    }
    char *words = malloc(1024);
    int status = 0;
    esp_err_t err = groq_transcribe(key, job->pcm, job->samples, words, 1024, &status);
    memset(key, 0, sizeof(key));
    ESP_LOGI(TAG, "voice: %.1f s of audio, transcribed in %d ms (HTTP %d)", job->samples / 16000.0f,
             (int)((esp_timer_get_time() - t0) / 1000), status);
    if (err != ESP_OK) {
        const char *why = status == 429                    ? "Groq limit reached. Try again later."
                          : (status == 401 || status == 403) ? "Groq didn't accept the saved key."
                          : status == 0                      ? "No connection to Groq."
                                                             : "Groq couldn't transcribe that.";
        emit(CLAUDE_EVT_PLATE, answer_plate(why, "Error"), NULL);
        free(words);
        return NULL;
    }
    ESP_LOGI(TAG, "heard: %s", words);
    if (!words[0]) {
        emit(CLAUDE_EVT_DONE, NULL, "didn't catch that");
        free(words);
        return NULL;
    }
    emit(CLAUDE_EVT_HEARD, NULL, words);
    return words;
}

static void worker(void *arg)
{
    (void)arg;
    body_t body = {.buf = heap_caps_malloc(BODY_MAX, MALLOC_CAP_SPIRAM), .len = 0};
    while (1) {
        job_t job;
        xQueueReceive(s_in, &job, portMAX_DELAY);
        s_busy = true;
        int64_t t0 = esp_timer_get_time();
        char *q = job.question ? job.question : transcribe(&job);
        if (q) {
            cJSON *plate = ask_claude(q, &body);
            free(q);
            if (!job.question) {
                ESP_LOGI(TAG, "voice: release to plate %d ms", (int)((esp_timer_get_time() - t0) / 1000));
            }
            emit(CLAUDE_EVT_PLATE, plate, NULL);
        }
        s_busy = false;
    }
}

esp_err_t claude_start(void)
{
    s_in = xQueueCreate(1, sizeof(job_t));
    s_out = xQueueCreate(4, sizeof(claude_evt_t));
    return xTaskCreate(worker, "claude", 12288, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}

static bool submit(job_t job)
{
    if (!s_in || s_busy || uxQueueMessagesWaiting(s_in)) {
        return false;
    }
    if (xQueueSend(s_in, &job, 0) != pdTRUE) {
        return false;
    }
    s_busy = true;
    return true;
}

bool claude_ask_voice(const int16_t *pcm, size_t samples)
{
    return submit((job_t){.pcm = pcm, .samples = samples});
}

bool claude_busy(void) { return s_busy; }

bool claude_take_event(claude_evt_t *e)
{
    return s_out && xQueueReceive(s_out, e, 0) == pdTRUE;
}
