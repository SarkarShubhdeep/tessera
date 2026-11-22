/* get_weather (review 09): Open-Meteo over HTTPS, trusted only through ISRG Root X1/X2.
 *
 * The user's place is geocoded once and saved (KEY_GEO); after that only coordinates rounded to 2 decimals
 * (about 1 km) go to api.open-meteo.com. Place names and coordinates are personal: never logged. */
#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "keys.h"
#include "net.h"
#include "weather_parse.h"

static const char *TAG = "weather";

extern const char WX_ROOTS[] asm("_binary_weather_roots_pem_start");

#define RESP_MAX (16 * 1024)

/* GET url into a malloc'd, NUL-terminated buffer. The URL holds a place name or coordinates: not logged. */
static char *get(const char *url, int *status)
{
    *status = 0;
    char *buf = heap_caps_calloc(1, RESP_MAX, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return NULL;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .cert_pem = WX_ROOTS,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        free(buf);
        return NULL;
    }
    int len = 0;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        *status = esp_http_client_get_status_code(c);
        int n;
        while (len < RESP_MAX - 1 && (n = esp_http_client_read(c, buf + len, RESP_MAX - 1 - len)) > 0) {
            len += n;
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (*status != 200) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static bool geocode(const char *setting, wx_place_t *out)
{
    char city[96], region[96], enc[300], url[400];
    wx_split_place(setting, city, sizeof(city), region, sizeof(region));
    wx_url_encode(city, enc, sizeof(enc));
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=10&language=en&format=json", enc);
    int status;
    int64_t t0 = esp_timer_get_time();
    char *json = get(url, &status);
    memset(url, 0, sizeof(url));
    memset(enc, 0, sizeof(enc));
    bool ok = json && wx_pick_place(json, region, out);
    ESP_LOGI(TAG, "place lookup: %s in %d ms (HTTP %d)", ok ? "found" : "not found",
             (int)((esp_timer_get_time() - t0) / 1000), status);
    free(json);
    memset(city, 0, sizeof(city));
    memset(region, 0, sizeof(region));
    return ok;
}

static char *error_text(bool *is_error, const char *why)
{
    *is_error = true;
    return strdup(why);
}

char *weather_tool(const char *other, bool *is_error)
{
    *is_error = false;
    if (!net_is_up()) {
        return error_text(is_error, "No Wi-Fi: the weather service can't be reached.");
    }
    wx_place_t p;
    if (other && *other) {
        if (!geocode(other, &p)) {
            return error_text(is_error, "That place wasn't found by the weather service.");
        }
    } else {
        char saved[KEYS_MAX + 1];
        if (keys_get(KEY_GEO, saved, sizeof(saved)) != KEY_SRC_NONE && wx_place_from_str(saved, &p)) {
            memset(saved, 0, sizeof(saved));
        } else {
            if (keys_get(KEY_PLACE, saved, sizeof(saved)) == KEY_SRC_NONE) {
                return error_text(is_error, "The user's place isn't set. They can set it on the setup page.");
            }
            bool found = geocode(saved, &p);
            memset(saved, 0, sizeof(saved));
            if (!found) {
                return error_text(is_error, "The user's place wasn't found by the weather service. They can "
                                            "check its spelling on the setup page (\"City, Region\").");
            }
            char geo[256];
            wx_place_to_str(&p, geo, sizeof(geo));
            keys_set(KEY_GEO, geo);   /* once: next time no lookup */
            memset(geo, 0, sizeof(geo));
        }
    }

    bool imp = wx_imperial(&p);
    char url[600];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,weather_code,"
             "wind_speed_10m&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
             "weather_code&timezone=auto&forecast_days=2&temperature_unit=%s&wind_speed_unit=%s"
             "&precipitation_unit=%s",
             p.lat, p.lon, imp ? "fahrenheit" : "celsius", imp ? "mph" : "kmh", imp ? "inch" : "mm");
    int status;
    int64_t t0 = esp_timer_get_time();
    char *json = get(url, &status);
    memset(url, 0, sizeof(url));
    char *result = json ? wx_result(json, &p) : NULL;
    free(json);
    ESP_LOGI(TAG, "forecast: %s in %d ms (HTTP %d)", result ? "ok" : "failed",
             (int)((esp_timer_get_time() - t0) / 1000), status);
    memset(&p, 0, sizeof(p));
    if (!result) {
        return error_text(is_error, status == 429 ? "The weather service's daily limit was reached."
                                                  : "The weather service didn't answer. Try again shortly.");
    }
    return result;
}
