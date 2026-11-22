/* Setup from a phone (review 07): the private hotspot and its session.
 *
 * - Started only from the Setup plate. Station + access point mode, so the home connection stays up.
 * - Hotspot "Tessera-XXXX", WPA2/WPA3, a 16-character random password, at most one device.
 * - A 128-bit random token in the page link.
 * - Ends on Done, Cancel or after 5 minutes: web server stopped, hotspot off, password and token wiped.
 * - Never logs the password or the token. */
#include "setup_ap.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net.h"
#include "setup_web.h"

static const char *TAG = "setup";

#define SESSION_S   300
#define PASS_LEN    16

typedef enum { CMD_START, CMD_STOP } cmd_kind_t;
typedef struct {
    cmd_kind_t kind;
    setup_end_t why;
} cmd_t;

static QueueHandle_t s_cmds;
static SemaphoreHandle_t s_lock;
static esp_netif_t *s_ap_netif;
static setup_view_t s_view;
static uint32_t s_version;
static char s_pass[PASS_LEN + 1];
static char s_token[SETUP_TOKEN_LEN + 1];
static int64_t s_deadline_us;
static bool s_wifi_changed;
static bool s_started_wifi;   /* the hotspot started Wi-Fi itself (no home network saved) */

static void set_stage(setup_stage_t st, const char *qr)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_view.stage = st;
    memset(s_view.qr, 0, sizeof(s_view.qr));
    if (qr) {
        strlcpy(s_view.qr, qr, sizeof(s_view.qr));
    }
    s_version++;
    xSemaphoreGive(s_lock);
}

uint32_t setup_get_view(setup_view_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_view;
    if (s_view.stage != SETUP_IDLE && s_view.stage != SETUP_ENDED) {
        int64_t left = (s_deadline_us - esp_timer_get_time()) / 1000000;
        out->seconds_left = left < 0 ? 0 : (int)left;
    }
    uint32_t v = s_version;
    xSemaphoreGive(s_lock);
    return v;
}

bool setup_token_matches(const char *s, size_t len)
{
    if (len < SETUP_TOKEN_LEN || !s_token[0]) {
        return false;
    }
    uint8_t diff = 0;
    for (int i = 0; i < SETUP_TOKEN_LEN; i++) {
        diff |= (uint8_t)(s[i] ^ s_token[i]);
    }
    return diff == 0;
}

uint32_t setup_ap_ip(void)
{
    esp_netif_ip_info_t ip;
    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &ip) == ESP_OK) {
        return ip.ip.addr;
    }
    return 0;
}

void setup_page_opened(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool first = s_view.stage == SETUP_OPEN || s_view.stage == SETUP_JOIN;
    xSemaphoreGive(s_lock);
    if (first) {
        ESP_LOGI(TAG, "page opened");
        set_stage(SETUP_PAGE, NULL);
    }
}

void setup_wifi_changed(void) { s_wifi_changed = true; }

void setup_request_start(void)
{
    cmd_t c = {.kind = CMD_START};
    if (s_cmds) {
        xQueueSend(s_cmds, &c, 0);
    }
}

void setup_request_stop(setup_end_t why)
{
    cmd_t c = {.kind = CMD_STOP, .why = why};
    if (s_cmds) {
        xQueueSend(s_cmds, &c, 0);
    }
}

static void random_secret(char *out, size_t n, const char *alphabet)
{
    size_t a = strlen(alphabet);
    for (size_t i = 0; i < n; i++) {
        uint32_t r;
        do {
            r = esp_random() & 0xff;
        } while (r >= 256 - (256 % a));   /* no bias */
        out[i] = alphabet[r % a];
    }
    out[n] = '\0';
}

static void url_qr(char *out, size_t len)
{
    esp_ip4_addr_t ip = {.addr = setup_ap_ip()};
    snprintf(out, len, "http://" IPSTR "/s/%s", IP2STR(&ip), s_token);
}

static void on_ap_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    setup_stage_t st = s_view.stage;
    xSemaphoreGive(s_lock);
    if (id == WIFI_EVENT_AP_STACONNECTED && st == SETUP_JOIN) {
        ESP_LOGI(TAG, "a phone joined the hotspot");
        char url[96];
        url_qr(url, sizeof(url));
        set_stage(SETUP_OPEN, url);
        memset(url, 0, sizeof(url));
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED && (st == SETUP_OPEN || st == SETUP_PAGE)) {
        ESP_LOGI(TAG, "the phone left the hotspot");
        char qr[96];
        snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", s_view.ssid, s_pass);
        set_stage(SETUP_JOIN, qr);
        memset(qr, 0, sizeof(qr));
    }
}

static esp_err_t start_session(void)
{
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[16];
    snprintf(ssid, sizeof(ssid), "Tessera-%02X%02X", mac[4], mac[5]);

    /* Fresh secrets every time. The radio is on, so the hardware RNG is a true random source. */
    random_secret(s_pass, PASS_LEN, "abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789");
    random_secret(s_token, SETUP_TOKEN_LEN, "0123456789abcdef");

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ssid);
    memcpy(ap.ap.password, s_pass, PASS_LEN);
    ap.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    ap.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ap.ap.pmf_cfg.required = false;
    ap.ap.max_connection = 1;
    ap.ap.channel = 1;
    wifi_ap_record_t home;
    if (esp_wifi_sta_get_ap_info(&home) == ESP_OK) {
        ap.ap.channel = home.primary;   /* station + AP share one radio channel */
    }

    s_started_wifi = !net_started();
    esp_err_t err = esp_wifi_set_mode(s_started_wifi ? WIFI_MODE_AP : WIFI_MODE_APSTA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    memset(&ap, 0, sizeof(ap));
    if (err == ESP_OK && s_started_wifi) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        err = setup_web_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "couldn't start the hotspot (%s)", esp_err_to_name(err));
        return err;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_view.ssid, ssid, sizeof(s_view.ssid));
    s_deadline_us = esp_timer_get_time() + (int64_t)SESSION_S * 1000000;
    xSemaphoreGive(s_lock);
    s_wifi_changed = false;

    char qr[96];
    snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", ssid, s_pass);
    set_stage(SETUP_JOIN, qr);
    memset(qr, 0, sizeof(qr));
    ESP_LOGI(TAG, "hotspot on (%s, %d min)", ssid, SESSION_S / 60);
    return ESP_OK;
}

static void end_session(setup_end_t why)
{
    setup_web_stop();
    if (s_started_wifi) {
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
    } else {
        esp_wifi_set_mode(WIFI_MODE_STA);   /* the hotspot goes off; the home connection stays */
    }
    memset(s_pass, 0, sizeof(s_pass));
    memset(s_token, 0, sizeof(s_token));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_view.end = why;
    xSemaphoreGive(s_lock);
    set_stage(SETUP_ENDED, NULL);
    static const char *const WHY[] = {"done", "cancelled", "timed out", "failed"};
    ESP_LOGI(TAG, "hotspot off (%s)", WHY[why]);
    if (s_wifi_changed || s_started_wifi) {
        s_wifi_changed = false;
        net_apply();   /* Wi-Fi changes from the page take effect now, so the phone wasn't dropped mid-save */
    }
}

static void setup_task(void *arg)
{
    (void)arg;
    while (1) {
        cmd_t c;
        bool got = xQueueReceive(s_cmds, &c, pdMS_TO_TICKS(1000)) == pdTRUE;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        setup_stage_t st = s_view.stage;
        xSemaphoreGive(s_lock);
        bool running = st == SETUP_JOIN || st == SETUP_OPEN || st == SETUP_PAGE;
        if (got && c.kind == CMD_START && !running) {
            if (start_session() != ESP_OK) {
                end_session(SETUP_END_FAILED);
            }
        } else if (got && c.kind == CMD_STOP && running) {
            vTaskDelay(pdMS_TO_TICKS(300));   /* let the web server finish the reply to Done */
            end_session(c.why);
        } else if (running && esp_timer_get_time() >= s_deadline_us) {
            end_session(SETUP_END_TIMEOUT);
        }
    }
}

esp_err_t setup_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_cmds = xQueueCreate(4, sizeof(cmd_t));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, on_ap_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, on_ap_event, NULL));
    return xTaskCreate(setup_task, "setup", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}
