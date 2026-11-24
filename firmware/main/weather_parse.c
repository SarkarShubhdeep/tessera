/* Weather (review 09): parsing and formatting, no ESP-IDF. Tested on the Mac against saved responses. */
#include "weather_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

void wx_split_place(const char *setting, char *city, size_t clen, char *region, size_t rlen)
{
    const char *comma = strchr(setting, ',');
    size_t n = comma ? (size_t)(comma - setting) : strlen(setting);
    while (n && setting[n - 1] == ' ') {
        n--;
    }
    snprintf(city, clen, "%.*s", (int)n, setting);
    region[0] = '\0';
    if (comma) {
        const char *r = comma + 1;
        while (*r == ' ') {
            r++;
        }
        snprintf(region, rlen, "%s", r);
        size_t m = strlen(region);
        while (m && region[m - 1] == ' ') {
            region[--m] = '\0';
        }
    }
}

void wx_url_encode(const char *in, char *out, size_t len)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < len; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            out[o++] = (char)*p;
        } else {
            out[o++] = '%';
            out[o++] = HEX[*p >> 4];
            out[o++] = HEX[*p & 15];
        }
    }
    out[o] = '\0';
}

/* Case-insensitive: does a contain b? */
static bool icontains(const char *a, const char *b)
{
    if (!a || !b || !*b) {
        return false;
    }
    size_t n = strlen(b);
    for (; *a; a++) {
        if (strncasecmp(a, b, n) == 0) {
            return true;
        }
    }
    return false;
}

static void copy_str(cJSON *o, const char *k, char *out, size_t len)
{
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
    snprintf(out, len, "%s", v ? v : "");
}

bool wx_pick_place(const char *geocode_json, const char *region, wx_place_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *j = cJSON_Parse(geocode_json);
    cJSON *results = cJSON_GetObjectItem(j, "results");
    cJSON *pick = NULL, *r;
    cJSON_ArrayForEach(r, results) {
        if (!pick) {
            pick = r;   /* the fallback: the geocoder's first (most relevant) result */
        }
        if (region && *region) {
            const char *a = cJSON_GetStringValue(cJSON_GetObjectItem(r, "admin1"));
            const char *c = cJSON_GetStringValue(cJSON_GetObjectItem(r, "country"));
            const char *cc = cJSON_GetStringValue(cJSON_GetObjectItem(r, "country_code"));
            if (icontains(a, region) || icontains(region, a) || icontains(c, region) ||
                (cc && strcasecmp(cc, region) == 0)) {
                pick = r;
                break;
            }
        }
    }
    bool ok = pick && cJSON_IsNumber(cJSON_GetObjectItem(pick, "latitude")) &&
              cJSON_IsNumber(cJSON_GetObjectItem(pick, "longitude"));
    if (ok) {
        out->lat = cJSON_GetNumberValue(cJSON_GetObjectItem(pick, "latitude"));
        out->lon = cJSON_GetNumberValue(cJSON_GetObjectItem(pick, "longitude"));
        copy_str(pick, "country_code", out->cc, sizeof(out->cc));
        copy_str(pick, "name", out->name, sizeof(out->name));
        copy_str(pick, "admin1", out->admin1, sizeof(out->admin1));
        copy_str(pick, "country", out->country, sizeof(out->country));
    }
    cJSON_Delete(j);
    return ok;
}

void wx_place_to_str(const wx_place_t *p, char *out, size_t len)
{
    snprintf(out, len, "%.4f,%.4f,%s|%s|%s|%s", p->lat, p->lon, p->cc, p->name, p->admin1, p->country);
}

bool wx_place_from_str(const char *s, wx_place_t *out)
{
    memset(out, 0, sizeof(*out));
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s);
    char *f[4] = {0};
    char *save = NULL;
    char *head = strtok_r(buf, "|", &save);
    for (int i = 1; i < 4; i++) {
        f[i] = strtok_r(NULL, "|", &save);
    }
    if (!head || sscanf(head, "%lf,%lf,%3s", &out->lat, &out->lon, out->cc) < 2) {
        return false;
    }
    snprintf(out->name, sizeof(out->name), "%s", f[1] ? f[1] : "");
    snprintf(out->admin1, sizeof(out->admin1), "%s", f[2] ? f[2] : "");
    snprintf(out->country, sizeof(out->country), "%s", f[3] ? f[3] : "");
    return true;
}

bool wx_imperial(const wx_place_t *p) { return strcmp(p->cc, "US") == 0; }

const char *wx_code_text(int code)
{
    switch (code) {
    case 0: return "clear sky";
    case 1: return "mainly clear";
    case 2: return "partly cloudy";
    case 3: return "overcast";
    case 45: case 48: return "fog";
    case 51: case 53: case 55: return "drizzle";
    case 56: case 57: return "freezing drizzle";
    case 61: return "light rain";
    case 63: return "rain";
    case 65: return "heavy rain";
    case 66: case 67: return "freezing rain";
    case 71: return "light snow";
    case 73: return "snow";
    case 75: return "heavy snow";
    case 77: return "snow grains";
    case 80: return "light rain showers";
    case 81: return "rain showers";
    case 82: return "violent rain showers";
    case 85: case 86: return "snow showers";
    case 95: return "thunderstorm";
    case 96: case 99: return "thunderstorm with hail";
    default: return "unknown";
    }
}

static double num(cJSON *o, const char *k) { return cJSON_GetNumberValue(cJSON_GetObjectItem(o, k)); }

static cJSON *day(cJSON *daily, int i)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "date", cJSON_GetStringValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "time"), i)) ?: "");
    cJSON_AddNumberToObject(d, "high", cJSON_GetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_max"), i)));
    cJSON_AddNumberToObject(d, "low", cJSON_GetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_min"), i)));
    cJSON *pp = cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "precipitation_probability_max"), i);
    if (cJSON_IsNumber(pp)) {
        cJSON_AddNumberToObject(d, "chance_of_rain_percent", cJSON_GetNumberValue(pp));
    }
    cJSON_AddStringToObject(d, "conditions",
                            wx_code_text((int)cJSON_GetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "weather_code"), i))));
    return d;
}

char *wx_result(const char *forecast_json, const wx_place_t *p)
{
    cJSON *j = cJSON_Parse(forecast_json);
    cJSON *cur = cJSON_GetObjectItem(j, "current");
    cJSON *daily = cJSON_GetObjectItem(j, "daily");
    if (!cur || !daily) {
        cJSON_Delete(j);
        return NULL;
    }
    bool imp = wx_imperial(p);
    cJSON *r = cJSON_CreateObject();
    char where[200];
    snprintf(where, sizeof(where), "%s%s%s, %s", p->name, p->admin1[0] ? ", " : "", p->admin1, p->country);
    cJSON_AddStringToObject(r, "place", where);
    cJSON_AddStringToObject(r, "source", "Open-Meteo");
    cJSON *u = cJSON_AddObjectToObject(r, "units");
    cJSON_AddStringToObject(u, "temperature", imp ? "°F" : "°C");
    cJSON_AddStringToObject(u, "wind", imp ? "mph" : "km/h");
    cJSON_AddStringToObject(u, "precipitation", imp ? "inch" : "mm");
    cJSON *now = cJSON_AddObjectToObject(r, "now");
    cJSON_AddStringToObject(now, "local_time_of_reading", cJSON_GetStringValue(cJSON_GetObjectItem(cur, "time")) ?: "");
    cJSON_AddNumberToObject(now, "temperature", num(cur, "temperature_2m"));
    cJSON_AddNumberToObject(now, "feels_like", num(cur, "apparent_temperature"));
    cJSON_AddNumberToObject(now, "humidity_percent", num(cur, "relative_humidity_2m"));
    cJSON_AddNumberToObject(now, "wind", num(cur, "wind_speed_10m"));
    cJSON_AddNumberToObject(now, "precipitation", num(cur, "precipitation"));
    cJSON_AddStringToObject(now, "conditions", wx_code_text((int)num(cur, "weather_code")));
    cJSON_AddItemToObject(r, "today", day(daily, 0));
    cJSON_AddItemToObject(r, "tomorrow", day(daily, 1));
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    cJSON_Delete(j);
    return s;
}
