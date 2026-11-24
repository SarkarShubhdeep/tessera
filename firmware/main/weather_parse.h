/* Weather (review 09): the parts with no ESP-IDF in them, so they can be tested on a Mac. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    double lat, lon;
    char cc[4];          /* country code, e.g. "US" */
    char name[64];
    char admin1[64];     /* region / state */
    char country[64];
} wx_place_t;

/* "City, Region" -> "City" and "Region" (region empty if there's no comma). */
void wx_split_place(const char *setting, char *city, size_t clen, char *region, size_t rlen);

/* Percent-encode for a URL query value. */
void wx_url_encode(const char *in, char *out, size_t len);

/* From Open-Meteo's geocoding JSON: the first result whose region or country matches region (case-insensitive,
 * either containing the other), else the first result. False if there are no results. */
bool wx_pick_place(const char *geocode_json, const char *region, wx_place_t *out);

/* Saved form: "lat,lon,CC|name|admin1|country". Personal: never logged. */
void wx_place_to_str(const wx_place_t *p, char *out, size_t len);
bool wx_place_from_str(const char *s, wx_place_t *out);

/* °F, mph and inches in the United States; °C, km/h and mm elsewhere. */
bool wx_imperial(const wx_place_t *p);

/* The tool_result for Claude, built from Open-Meteo's forecast JSON. malloc'd; NULL if it can't be read. */
char *wx_result(const char *forecast_json, const wx_place_t *p);

/* WMO weather code in words. */
const char *wx_code_text(int code);
