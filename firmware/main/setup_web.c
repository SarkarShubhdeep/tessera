/* The setup page's web server (review 07).
 *
 * Every connection, whatever it asks for:
 *  1. must have arrived on the hotspot's own address (192.168.4.1). A connection that came in over the home
 *     network is closed the moment it's accepted, before anything is read or answered;
 *  2. must start with /s/<token>, the one-time link from the QR code. Otherwise: 404.
 * Then: GET /s/<token> (the page), GET /s/<token>/status, POST /s/<token>/save, POST /s/<token>/done.
 * Saves go through keys_set, the same checks as the console. Nothing is logged but setting names and lengths. */
#include "setup_web.h"

#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "clock.h"
#include "keys.h"
#include "lwip/sockets.h"
#include "setup_ap.h"

static const char *TAG = "setup";

extern const char PAGE[] asm("_binary_setup_page_html_start");

#define BODY_MAX 1024

static httpd_handle_t s_server;

static const struct {
    const char *id;
    key_id_t key;
    const char *name;
} SETTINGS[] = {
    {"wifi_name", KEY_WIFI_NAME, "wifi name"},
    {"wifi_pass", KEY_WIFI_PASS, "wifi password"},
    {"claude", KEY_CLAUDE, "claude key"},
    {"groq", KEY_GROQ, "groq key"},
    {"tz", KEY_TZ, "time zone"},
    {"place", KEY_PLACE, "place"},
};
#define N_SETTINGS (sizeof(SETTINGS) / sizeof(SETTINGS[0]))

/* The local address a connection arrived on, as IPv4 (the server socket is IPv6, so IPv4 shows up mapped). */
static bool sock_on_hotspot(int sockfd)
{
    uint32_t ap = setup_ap_ip();
    struct sockaddr_storage local;
    socklen_t n = sizeof(local);
    if (!ap || getsockname(sockfd, (struct sockaddr *)&local, &n) != 0) {
        return false;
    }
    uint32_t got = 0;
    if (local.ss_family == AF_INET) {
        got = ((struct sockaddr_in *)&local)->sin_addr.s_addr;
    } else if (local.ss_family == AF_INET6) {
        const uint8_t *b = ((struct sockaddr_in6 *)&local)->sin6_addr.s6_addr;
        static const uint8_t MAPPED[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (memcmp(b, MAPPED, 12) != 0) {
            return false;
        }
        memcpy(&got, b + 12, 4);
    }
    return got == ap;
}

/* Checked twice: when a connection is accepted (so the server never answers anything on the home network,
 * not even an error for an unknown method), and again for every request. */
static esp_err_t on_open(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    return sock_on_hotspot(sockfd) ? ESP_OK : ESP_FAIL;   /* ESP_FAIL: closed at once, nothing read */
}

static void security_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                       "connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
}

static esp_err_t send_json(httpd_req_t *req, cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    security_headers(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, s ? s : "{}");
    cJSON_free(s);
    return err;
}

static esp_err_t status(httpd_req_t *req)
{
    static const char *const SRC[] = {"missing", "saved in tessera", "from ESP-Claw"};
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < N_SETTINGS; i++) {
        key_src_t src;
        size_t len = keys_len(SETTINGS[i].key, &src);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", SETTINGS[i].id);
        cJSON_AddStringToObject(o, "state", SRC[src]);
        cJSON_AddNumberToObject(o, "len", (double)len);
        cJSON_AddItemToArray(arr, o);
    }
    return send_json(req, arr);
}

static esp_err_t reply(httpd_req_t *req, bool ok, const char *msg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", ok);
    cJSON_AddStringToObject(o, "message", msg);
    return send_json(req, o);
}

static esp_err_t save(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > BODY_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad size");
        return ESP_OK;
    }
    char body[BODY_MAX + 1];
    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) {
            memset(body, 0, sizeof(body));
            return ESP_FAIL;
        }
        got += r;
    }
    body[got] = '\0';
    cJSON *j = cJSON_Parse(body);
    memset(body, 0, sizeof(body));

    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(j, "setting"));
    cJSON *val = cJSON_GetObjectItem(j, "value");
    bool clear = cJSON_IsTrue(cJSON_GetObjectItem(j, "clear"));
    size_t i = 0;
    while (id && i < N_SETTINGS && strcmp(id, SETTINGS[i].id) != 0) {
        i++;
    }
    char msg[80];
    bool ok = false;
    if (!id || i == N_SETTINGS) {
        snprintf(msg, sizeof(msg), "unknown setting");
    } else if (clear) {
        ok = keys_clear(SETTINGS[i].key) == ESP_OK;
        snprintf(msg, sizeof(msg), "%s %s", SETTINGS[i].name, ok ? "cleared" : "could not be cleared");
    } else if (!cJSON_IsString(val) || !keys_valid(SETTINGS[i].key, val->valuestring)) {
        snprintf(msg, sizeof(msg), "that doesn't look like a valid %s; not saved", SETTINGS[i].name);
    } else {
        size_t len = strlen(val->valuestring);
        ok = keys_set(SETTINGS[i].key, val->valuestring) == ESP_OK;
        snprintf(msg, sizeof(msg), ok ? "%s saved (%u chars)" : "%s not saved", SETTINGS[i].name, (unsigned)len);
    }
    if (ok && i < N_SETTINGS && (SETTINGS[i].key == KEY_WIFI_NAME || SETTINGS[i].key == KEY_WIFI_PASS)) {
        setup_wifi_changed();   /* applied when setup ends, so the phone isn't dropped now */
    }
    if (ok && i < N_SETTINGS && SETTINGS[i].key == KEY_TZ) {
        clock_apply_tz();
    }
    if (cJSON_IsString(val)) {
        memset(val->valuestring, 0, strlen(val->valuestring));   /* wipe before freeing */
    }
    cJSON_Delete(j);
    ESP_LOGI(TAG, "phone: %s", msg);
    return reply(req, ok, msg);
}

static esp_err_t handle(httpd_req_t *req)
{
    if (!sock_on_hotspot(httpd_req_to_sockfd(req))) {
        return ESP_FAIL;   /* closes the connection without an answer */
    }
    const char *uri = req->uri;
    size_t len = strlen(uri);
    if (strncmp(uri, "/s/", 3) != 0 || !setup_token_matches(uri + 3, len - 3)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_OK;
    }
    const char *rest = uri + 3 + SETUP_TOKEN_LEN;
    const char *q = strchr(rest, '?');
    size_t rlen = q ? (size_t)(q - rest) : strlen(rest);

    if (req->method == HTTP_GET && rlen == 0) {
        setup_page_opened();
        security_headers(req);
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_sendstr(req, PAGE);
    }
    if (req->method == HTTP_GET && rlen == 7 && strncmp(rest, "/status", 7) == 0) {
        return status(req);
    }
    if (req->method == HTTP_POST && rlen == 5 && strncmp(rest, "/save", 5) == 0) {
        return save(req);
    }
    if (req->method == HTTP_POST && rlen == 5 && strncmp(rest, "/done", 5) == 0) {
        esp_err_t err = reply(req, true, "setup finished");
        setup_request_stop(SETUP_END_DONE);
        return err;
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    return ESP_OK;
}

esp_err_t setup_web_start(void)
{
    if (s_server) {
        return ESP_OK;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_open_sockets = 2;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;
    cfg.stack_size = 6144;
    cfg.max_uri_handlers = 2;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.open_fn = on_open;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        s_server = NULL;
        return err;
    }
    static const httpd_uri_t get = {.uri = "/*", .method = HTTP_GET, .handler = handle};
    static const httpd_uri_t post = {.uri = "/*", .method = HTTP_POST, .handler = handle};
    httpd_register_uri_handler(s_server, &get);
    httpd_register_uri_handler(s_server, &post);
    return ESP_OK;
}

void setup_web_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
