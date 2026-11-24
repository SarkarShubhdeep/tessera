/* get_weather (review 09): Open-Meteo, called from Claude's worker task. */
#pragma once

#include <stdbool.h>

/* Weather for the user's place (other == NULL or "") or another place Claude names. Returns a malloc'd
 * tool_result text: the weather as JSON, or a plain reason when *is_error is set. Blocking. */
char *weather_tool(const char *other, bool *is_error);
