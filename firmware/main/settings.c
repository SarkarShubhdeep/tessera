/* Read-only access to the settings ESP-Claw saved (NVS partition "nvs", namespace "app").
 * Key names from ESP-Claw's source (application/edge_agent/components/app_config/app_config.c). */
#include "settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

static void get(nvs_handle_t h, const char *key, char *dst, size_t len)
{
    size_t n = len;
    dst[0] = '\0';
    if (nvs_get_str(h, key, dst, &n) != ESP_OK) {
        dst[0] = '\0';
    }
}

esp_err_t settings_load(tsr_settings_t *out)
{
    memset(out, 0, sizeof(*out));
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        /* No erase-and-retry: that would wipe ESP-Claw's settings. */
        ESP_LOGE(TAG, "settings storage needs repair (%s); not touching it", esp_err_to_name(err));
        return err;
    }
    nvs_handle_t h;
    err = nvs_open("app", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no saved ESP-Claw settings (%s)", esp_err_to_name(err));
        return err;
    }
    get(h, "wifi_ssid", out->wifi_ssid, sizeof(out->wifi_ssid));
    get(h, "wifi_password", out->wifi_password, sizeof(out->wifi_password));
    get(h, "llm_api_key", out->api_key, sizeof(out->api_key));
    get(h, "llm_model", out->model, sizeof(out->model));
    get(h, "llm_backend", out->backend, sizeof(out->backend));
    nvs_close(h);

    /* Presence and length only: never print a value except the model and backend names. */
    ESP_LOGI(TAG, "wifi network: %s (%u chars)", out->wifi_ssid[0] ? "found" : "missing",
             (unsigned)strlen(out->wifi_ssid));
    ESP_LOGI(TAG, "wifi password: %s", out->wifi_password[0] ? "found" : "missing");
    ESP_LOGI(TAG, "api key: %s, %s", out->api_key[0] ? "found" : "missing",
             settings_key_is_claude(out) ? "a Claude key" : "not a Claude key");
    ESP_LOGI(TAG, "ESP-Claw backend: %s, model: %s", out->backend[0] ? out->backend : "(none)",
             out->model[0] ? out->model : "(none)");
    return (out->wifi_ssid[0] && out->api_key[0]) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool settings_key_is_claude(const tsr_settings_t *s)
{
    return strncmp(s->api_key, "sk-ant-", 7) == 0;
}
