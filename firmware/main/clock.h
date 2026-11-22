/* The board's clock (review 08): SNTP after Wi-Fi, local time from the time zone setting. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Start SNTP (pool.ntp.org) and apply the saved time zone. Once, after net_init. */
void clock_start(void);

/* Re-read the time zone setting (after it's saved or cleared). */
void clock_apply_tz(void);

/* True once the clock has been set from the internet. */
bool clock_synced(void);

/* Wait up to timeout_ms for the first sync. */
bool clock_wait(int timeout_ms);

/* Local hour and minute, for the ring. False until synced. */
bool clock_hm(int *hour, int *minute);

/* "Sunday 22 November 2026, 13:52 (America/New_York)" for Claude, or a line saying the time is unknown. */
void clock_line(char *out, size_t len);
