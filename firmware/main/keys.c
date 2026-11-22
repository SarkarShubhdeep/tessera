/* Tessera's own saved settings (reviews 05 and 06): Wi-Fi name and password, the Claude key and the Groq
 * key, in NVS namespace "tessera".
 *
 * This is the ONLY file that writes NVS. It writes only the "tessera" namespace, one entry at a time, and
 * only when the owner types a command on the console. ESP-Claw's namespace "app" is opened here read-only,
 * as a fallback. No erase of the partition or of a whole namespace anywhere. Nothing is written at boot.
 *
 * Values are never logged: only whether each is saved, its length, and where it comes from. */
#include "keys.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "keys";

#define NS     "tessera"
#define NS_APP "app"   /* ESP-Claw's: read only */

typedef struct {
    const char *ours;     /* entry in "tessera" */
    const char *theirs;   /* entry in ESP-Claw's "app", or NULL */
    const char *prefix;   /* required prefix, or NULL */
    size_t min, max;
    bool token;           /* letters, digits, _ and - only */
} spec_t;

static const spec_t SPEC[KEY_COUNT] = {
    [KEY_WIFI_NAME] = {"wifi_ssid", "wifi_ssid", NULL, 1, 32, false},
    [KEY_WIFI_PASS] = {"wifi_pass", "wifi_password", NULL, 8, 63, false},
    [KEY_CLAUDE] = {"claude_key", "llm_api_key", "sk-ant-", 20, KEYS_MAX, true},
    [KEY_GROQ] = {"groq_key", NULL, "gsk_", 20, 200, true},
};

void keys_log_stats(const char *when)
{
    nvs_stats_t st;
    if (nvs_get_stats(NULL, &st) == ESP_OK) {
        ESP_LOGI(TAG, "settings storage %s: %u used, %u free of %u entries", when, (unsigned)st.used_entries,
                 (unsigned)st.free_entries, (unsigned)st.total_entries);
    }
}

/* Read one string from a namespace, read-only: if the namespace doesn't exist, this creates nothing. */
static bool read_str(const char *ns, const char *entry, char *out, size_t len)
{
    out[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t n = len;
    esp_err_t err = nvs_get_str(h, entry, out, &n);
    nvs_close(h);
    if (err != ESP_OK || !out[0]) {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool keys_valid(key_id_t id, const char *value)
{
    const spec_t *s = &SPEC[id];
    size_t n = strlen(value);
    if (n < s->min || n > s->max) {
        return false;
    }
    if (s->prefix && strncmp(value, s->prefix, strlen(s->prefix)) != 0) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x20 || c == 0x7f) {
            return false;   /* no control characters in anything */
        }
        if (s->token && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                          c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

key_src_t keys_get(key_id_t id, char *out, size_t len)
{
    const spec_t *s = &SPEC[id];
    if (read_str(NS, s->ours, out, len)) {
        return KEY_SRC_TESSERA;
    }
    if (s->theirs && read_str(NS_APP, s->theirs, out, len)) {
        /* ESP-Claw may have saved another provider's key there: only use it if it fits. */
        if (keys_valid(id, out)) {
            return KEY_SRC_ESPCLAW;
        }
        memset(out, 0, len);
    }
    return KEY_SRC_NONE;
}

size_t keys_len(key_id_t id, key_src_t *src)
{
    char v[KEYS_MAX + 1];
    key_src_t s = keys_get(id, v, sizeof(v));
    size_t n = s == KEY_SRC_NONE ? 0 : strlen(v);
    memset(v, 0, sizeof(v));
    if (src) {
        *src = s;
    }
    return n;
}

esp_err_t keys_set(key_id_t id, const char *value)
{
    if (!keys_valid(id, value)) {
        return ESP_ERR_INVALID_ARG;
    }
    keys_log_stats("before saving");
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, SPEC[id].ours, value);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    keys_log_stats("after saving");
    return err;
}

esp_err_t keys_clear(key_id_t id)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return ESP_OK;   /* nothing saved, nothing to clear */
    }
    nvs_close(h);
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_key(h, SPEC[id].ours);   /* this one entry only */
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}
