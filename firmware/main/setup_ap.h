/* Setup from a phone (review 07): a private hotspot, opened from the Setup plate, closed after Done, Cancel or
 * 5 minutes. One task owns it; the UI, the web server and the timer only send it requests. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    SETUP_IDLE,
    SETUP_JOIN,    /* hotspot on; QR = the hotspot */
    SETUP_OPEN,    /* a phone joined; QR = the page link */
    SETUP_PAGE,    /* the page was opened on the phone */
    SETUP_ENDED,   /* just ended; reason in end */
} setup_stage_t;

typedef enum {
    SETUP_END_DONE,
    SETUP_END_CANCEL,
    SETUP_END_TIMEOUT,
    SETUP_END_FAILED,
} setup_end_t;

typedef struct {
    setup_stage_t stage;
    setup_end_t end;
    int seconds_left;
    char ssid[16];
    char qr[160];   /* what the QR code holds at this stage (secret while setup runs; RAM only) */
} setup_view_t;

/* Once, after net_init. */
esp_err_t setup_init(void);

/* From the UI. Start is ignored if setup is already running. */
void setup_request_start(void);
void setup_request_stop(setup_end_t why);

/* For the UI thread: copy the current view. Returns a number that changes whenever the stage changes. */
uint32_t setup_get_view(setup_view_t *out);

/* For the web server. */
bool setup_token_matches(const char *s, size_t len);   /* constant time */
uint32_t setup_ap_ip(void);                            /* the hotspot's address, network byte order */
void setup_page_opened(void);
void setup_wifi_changed(void);                          /* applied when setup ends */

#define SETUP_TOKEN_LEN 32   /* hex characters: 128 bits */
