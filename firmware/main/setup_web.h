/* The setup page's web server (review 07). Runs only while the hotspot is on. */
#pragma once

#include "esp_err.h"

esp_err_t setup_web_start(void);
void setup_web_stop(void);
