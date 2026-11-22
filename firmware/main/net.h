/* Wi-Fi station (review 04). Settings stay in RAM; nothing is saved to flash. */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Start Wi-Fi and connect; returns once connected or after timeout_ms. */
esp_err_t net_connect(const char *ssid, const char *password, int timeout_ms);
bool net_is_up(void);
