/* The board's clock (review 08).
 *
 * SNTP from pool.ntp.org once Wi-Fi is up, re-checked periodically by ESP-IDF (CONFIG_LWIP_SNTP_UPDATE_DELAY). The time zone setting is
 * "<label>|<POSIX TZ>" (from the setup pages) or a bare POSIX string (ESP-Claw's fallback); without one the
 * clock is UTC. The setting is personal (it says roughly where the owner lives), so it is never logged. */
#include "clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_netif_sntp.h"
#include "keys.h"

static const char *TAG = "clock";
static char s_label[64];   /* what Claude is told the zone is called */

void clock_apply_tz(void)
{
    char v[KEYS_MAX + 1];
    const char *posix = "UTC0";
    s_label[0] = '\0';
    if (keys_get(KEY_TZ, v, sizeof(v)) != KEY_SRC_NONE) {
        char *bar = strchr(v, '|');
        if (bar) {
            *bar = '\0';
            strlcpy(s_label, v, sizeof(s_label));
            posix = bar + 1;
        } else {
            posix = v;
            strlcpy(s_label, v, sizeof(s_label));
        }
    }
    setenv("TZ", posix, 1);
    tzset();
    memset(v, 0, sizeof(v));
    ESP_LOGI(TAG, "time zone %s", s_label[0] ? "set" : "not set (UTC)");
}

void clock_start(void)
{
    clock_apply_tz();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&cfg);
}

bool clock_synced(void)
{
    return time(NULL) > 1767225600;   /* after 2026-01-01: the RTC starts at 1970 */
}

bool clock_wait(int timeout_ms)
{
    if (clock_synced()) {
        return true;
    }
    esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    return clock_synced();
}

bool clock_hm(int *hour, int *minute)
{
    if (!clock_synced()) {
        return false;
    }
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    *hour = t.tm_hour;
    *minute = t.tm_min;
    return true;
}

void clock_line(char *out, size_t len)
{
    if (!clock_synced()) {
        snprintf(out, len, "The current date and time are unknown (the device clock isn't set).");
        return;
    }
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char when[64];
    strftime(when, sizeof(when), "%A %d %B %Y, %H:%M", &t);
    snprintf(out, len, "Now: %s (%s).", when, s_label[0] ? s_label : "UTC; the user's time zone isn't set");
}
