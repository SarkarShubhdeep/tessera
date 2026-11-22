/* Wi-Fi station (reviews 04 and 06). Settings stay in RAM; Wi-Fi itself saves nothing to flash. */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Set up the network stack and the Wi-Fi driver (station mode). Once, at boot. */
esp_err_t net_init(void);

/* (Re)connect with the saved Wi-Fi name and password (Tessera's, else ESP-Claw's). Starts Wi-Fi if it
 * isn't running; stops it if no name is saved anywhere. Call again after the settings change. */
esp_err_t net_apply(void);

/* Wait until connected, up to timeout_ms. */
bool net_wait(int timeout_ms);

/* True while the home-network station is running (a name is saved). */
bool net_started(void);

bool net_is_up(void);
