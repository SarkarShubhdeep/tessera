/* Wi-Fi station (reviews 04 and 06). The name and password come from keys_get (Tessera's, else
 * ESP-Claw's); the driver keeps them in RAM only, nothing is saved to flash by Wi-Fi itself. The network
 * name is never logged. */
#include "net.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "keys.h"

static const char *TAG = "net";
static EventGroupHandle_t s_ev;
static bool s_started;
#define UP_BIT BIT0

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_ev, UP_BIT);
        ESP_LOGW(TAG, "disconnected (reason %d); retrying", ((wifi_event_sta_disconnected_t *)data)->reason);
        if (s_started) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifi_ap_record_t ap;
        int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
        ESP_LOGI(TAG, "connected, signal %d dBm", rssi);
        xEventGroupSetBits(s_ev, UP_BIT);
    }
}

esp_err_t net_init(void)
{
    s_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    /* The Wi-Fi library logs the network name at INFO when it connects; keep it out of the log. */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   /* never save to NVS */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    return ESP_OK;
}

esp_err_t net_apply(void)
{
    wifi_config_t cfg = {0};
    char name[33], pass[65];   /* one more than the driver's fields: a 32-char name has no terminator there */
    key_src_t src = keys_get(KEY_WIFI_NAME, name, sizeof(name));
    if (src == KEY_SRC_NONE) {
        ESP_LOGW(TAG, "no wifi name saved (type: wifi name <name>)");
        if (s_started) {
            s_started = false;
            esp_wifi_stop();
            xEventGroupClearBits(s_ev, UP_BIT);
        }
        return ESP_ERR_NOT_FOUND;
    }
    /* An empty password means an open network. */
    keys_get(KEY_WIFI_PASS, pass, sizeof(pass));
    memcpy(cfg.sta.ssid, name, strnlen(name, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, pass, strnlen(pass, sizeof(cfg.sta.password)));
    memset(name, 0, sizeof(name));
    memset(pass, 0, sizeof(pass));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    memset(&cfg, 0, sizeof(cfg));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi settings not accepted (%s)", esp_err_to_name(err));
        return err;
    }
    if (!s_started) {
        s_started = true;
        ESP_LOGI(TAG, "starting wifi (network from %s)", src == KEY_SRC_TESSERA ? "tessera" : "ESP-Claw");
        return esp_wifi_start();   /* STA_START connects */
    }
    ESP_LOGI(TAG, "reconnecting with the new wifi settings");
    xEventGroupClearBits(s_ev, UP_BIT);
    esp_wifi_disconnect();   /* the disconnect handler connects again, with the new settings */
    return ESP_OK;
}

bool net_wait(int timeout_ms)
{
    EventBits_t b = xEventGroupWaitBits(s_ev, UP_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (b & UP_BIT) != 0;
}

bool net_is_up(void) { return s_ev && (xEventGroupGetBits(s_ev) & UP_BIT); }
