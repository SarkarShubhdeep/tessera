# Review 09: a weather tool (Open-Meteo), not web search

**Status:** draft 2026-11-22, waiting for approval. Nothing built yet.
**Build:** step 08 plus a weather tool. In review 08, asked for the current temperature, Claude made two web
searches and fetched one page. It took 35 s and still found no single current figure. This step gives
Claude a `get_weather` tool: the board asks a weather service directly and hands Claude exact numbers.
Issue [#21](https://github.com/SarkarShubhdeep/tessera/issues/21).

## Pins

**No new pins.** Software and network only.

## The service: Open-Meteo

- `api.open-meteo.com` for weather and `geocoding-api.open-meteo.com` to find a place. JSON over HTTPS, no
  account, no key.
- **Terms:** free for non-commercial use, under a daily request limit far above what one board makes, with
  credit to Open-Meteo (CC BY 4.0). Plates from this tool say `Open-Meteo` as their source. To check on
  open-meteo.com before relying on it for anything commercial.
- **Measured from the Mac** (with Berlin as the example, not the owner's place): weather 903 bytes in 0.11 s;
  geocoding 424 bytes in 0.52 s.
- **Certificates:** both hosts use Let's Encrypt. The chains end at **ISRG Root X2** (`api.`) and **ISRG
  Root X1** (`geocoding-api.`), not the Google Trust Services roots the board trusts today. Both ISRG roots
  are added in their own file, `certs/weather_roots.pem`, copied from ESP-IDF's Mozilla list, and used only
  for these two hosts. Anthropic and Groq keep their pinned GTS roots.

## What the board sends where

| When | To | What |
|---|---|---|
| Once, when the place is first needed, and again only if the place setting changes | `geocoding-api.open-meteo.com` | The place name, as you typed it |
| Each weather question | `api.open-meteo.com` | Coordinates **rounded to 2 decimals** (about 1 km), what to return, the units |
| Claude asks about another city ("weather in Paris") | Both | That city's name, then its coordinates |

- The coordinates are saved in Tessera's namespace (`geo`), with the matched place name and country, so
  geocoding happens once. They're personal like the place: never logged, never in the repo. Clearing the
  place clears them too.
- Nothing else goes to Open-Meteo: no key, no question text, no device ID. Plain HTTPS GET requests.

## The tool

```json
{"name": "get_weather",
 "description": "Current conditions and today's and tomorrow's forecast from a weather service. Use it for any weather question instead of web search. Without place it uses the user's own place.",
 "input_schema": {"type": "object", "properties": {"place": {"type": "string", "description": "Another city, only if the user names one"}}}}
```

**What the board returns to Claude** (a `tool_result`, short JSON):
- Where: the matched place name and country.
- Now: temperature, feels like, humidity, wind, precipitation, conditions in words, and the reading's time.
- Today and tomorrow: high, low, chance of rain, conditions.
- The units.

Conditions in words come from the WMO weather codes Open-Meteo uses (`3` → "overcast", `80` → "rain
showers"), mapped by a small table on the board.

**Units:** °F and mph if the place is in the United States (from the geocoder's country code), otherwise °C
and km/h. No new setting.

**Errors** come back to Claude as a `tool_result` with `is_error: true` and a plain reason: no place set,
place not found, service unreachable, no Wi-Fi. Claude then explains on a plate.

## The tool-use loop on the board

Today, Claude's `show_plate` call is the end of a question. `get_weather` is different, because Claude
needs the answer before it can make the plate:

1. Claude replies with `stop_reason: "tool_use"` and a `get_weather` call.
2. The board appends Claude's turn, calls Open-Meteo, and sends a user turn with the `tool_result`.
3. Claude makes the plate.

At most **3** rounds in all, counting web search's `pause_turn` resumes, so a confused exchange can't loop.
The system prompt says: for weather, use `get_weather`, not web search; say the reading's time if it's not
recent. The tools stay in the same order every time, so prompt caching still works.

## Matching the place

Open-Meteo's geocoder takes a single name. For a setting like "City, Region", the board searches for the
part before the comma (up to 10 results), and picks the first whose region (`admin1`) or country matches
the part after it, otherwise the first result. The matched name and country are given to Claude, so a
wrong match shows on the plate ("Springfield, Illinois?") instead of hiding.

## Code changes

| File | Change |
|---|---|
| `main/weather.c` (new) | Geocode (once, saved), forecast request, WMO code table, JSON result for Claude |
| `main/certs/weather_roots.pem` (new) | ISRG Root X1 and X2 |
| `main/claude.c` | `get_weather` tool, the tool-use loop (3 rounds), prompt line |
| `main/keys.c` | `geo` entry (coordinates, name, country, US or not); cleared with the place |
| `main/CMakeLists.txt` | `weather.c`, the new certificate file |

## Checks

**Before (no board needed):**
- Builds with no warnings.
- The request check on the Mac: tools in a fixed order (`show_plate`, `get_weather`, `web_search`); the
  cached prefix is still identical between questions.
- `weather.c`'s JSON parsing and WMO table checked on the Mac against a saved Open-Meteo response (Berlin).
- No log line prints coordinates, the place or the matched name.
- The new certificate file is used only for the two Open-Meteo hosts.

**On the board:** app slot only, backup ready.
1. "What's the temperature here right now?": a plate with the current figure from Open-Meteo, and the
   time, tokens and rounds in the log. Target: a few seconds.
2. "Will it rain tomorrow?": the forecast, no web search.
3. "What's the weather in <another city>?": the other city, matched by name.
4. A question that isn't about weather: unchanged, and no `get_weather` call.

**Stop rule unchanged:** any buzz, any sound from the speaker, or a mismatched pin means hold POWER and
restore.
