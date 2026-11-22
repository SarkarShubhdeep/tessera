/* Tessera's own saved keys (review 05): the Groq key, in NVS namespace "tessera".
 *
 * This is the ONLY file that writes NVS. It writes only the "tessera" namespace, one entry, and only when
 * you type a `key groq ...` command. ESP-Claw's namespace "app" is never opened here. No erase of the
 * partition or of a whole namespace anywhere. Nothing is written at boot.
 *
 * The key is never logged: only whether it is saved and its length. */
#include "keys.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "keys";

#define NS      "tessera"
#define K_GROQ  "groq_key"

void keys_log_stats(const char *when)
{
    nvs_stats_t st;
    if (nvs_get_stats(NULL, &st) == ESP_OK) {
        ESP_LOGI(TAG, "settings storage %s: %u used, %u free of %u entries", when, (unsigned)st.used_entries,
                 (unsigned)st.free_entries, (unsigned)st.total_entries);
    }
}

bool keys_get_groq(char *out, size_t len)
{
    out[0] = '\0';
    nvs_handle_t h;
    /* Read-only open: if the namespace doesn't exist yet, this fails instead of creating it. */
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t n = len;
    esp_err_t err = nvs_get_str(h, K_GROQ, out, &n);
    nvs_close(h);
    if (err != ESP_OK) {
        out[0] = '\0';
        return false;
    }
    return true;
}

size_t keys_groq_len(void)
{
    char k[KEYS_GROQ_MAX + 1];
    size_t n = keys_get_groq(k, sizeof(k)) ? strlen(k) : 0;
    memset(k, 0, sizeof(k));
    return n;
}

bool keys_groq_valid(const char *key)
{
    size_t n = strlen(key);
    if (n < 20 || n > KEYS_GROQ_MAX || strncmp(key, "gsk_", 4) != 0) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = key[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                  c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

esp_err_t keys_set_groq(const char *key)
{
    if (!keys_groq_valid(key)) {
        return ESP_ERR_INVALID_ARG;
    }
    keys_log_stats("before saving");
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, K_GROQ, key);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    keys_log_stats("after saving");
    return err;
}

esp_err_t keys_clear_groq(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return ESP_OK;   /* nothing saved, nothing to clear */
    }
    nvs_close(h);
    err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_key(h, K_GROQ);   /* this one entry only */
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
