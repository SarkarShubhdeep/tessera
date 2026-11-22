# Review 08: live data (a real clock, your place, and web search)

**Status:** approved 2026-11-22, flashed (app slot only) and working, with two follow-ups (results at the end).
**Build:** step 07 plus live data. Today Claude can't answer "what's the weather" or "what time is it": it
has no web access and is never told the date, and the ring's clock is the simulator's fake 09:41. This
step sets the board's clock, tells Claude the local time and (if you set one) your place, and lets Claude
search the web. Issue [#19](https://github.com/SarkarShubhdeep/tessera/issues/19).

## Pins

**No new pins.** Everything here is software and network.

## 1. The clock

- After Wi-Fi connects, the board sets its clock with **SNTP** (ESP-IDF's `esp_sntp`) from
  `pool.ntp.org`, and checks it again every few hours.
- The ring's clock shows the real local time. Until the first sync it shows `--:--`.
- **Time zone:** a new setting (below). With none set, the clock shows UTC and says so to Claude.
- SNTP is plain, unauthenticated UDP. A network attacker could feed the board a wrong time. The worst
  outcome is a wrong clock and a wrong "today" for Claude; it can't break HTTPS, because the certificate
  check would then fail and say so. The certificate check needs a roughly right clock anyway, so the
  board waits for the first sync before its first question, at most 10 s.

## 2. Time zone and place: two new settings

| Setting | Example | Used for | Required |
|---|---|---|---|
| Time zone | `America/New_York` | The clock; "it's 9:41 on Sunday" for Claude | No (UTC without it) |
| Place | a city and region, as you'd say it | "the weather here", "what's open near me" | No |

- Both go on **both setup pages** (USB and phone), with the same Save and Clear.
- **Time zone:** the page offers the zone your computer or phone is in (the browser knows it) and you
  confirm it. The board needs it in POSIX form (`EST5EDT,M3.2.0,M11.1.0`); the page converts the common
  zones with a small built-in table, and has a field for typing a POSIX string for anything else.
- Stored in Tessera's namespace like the keys (`tz`, `place`). Both are personal (they say roughly where
  you live), so the log treats them like keys: saved, missing or cleared, and a length. Never the
  value. The setup pages show them back to you, because they aren't secrets from you.
- The place is sent to Anthropic with each question, as part of the instructions to Claude. That's the
  point of it; leave it empty if you'd rather not.

## 3. Claude: the time, the place, and web search

**Every question** now starts Claude's instructions with a line like:
`Now: Sunday 22 November 2026, 13:52 (America/New_York). The user is in <place>.`

**Web search:** Anthropic's server-side tool, `{"type": "web_search_20260209", "name": "web_search",
"max_uses": 3}`.
- Anthropic runs the search and filters the results itself ("dynamic filtering"); the board only sends the
  question and gets the final answer back. No search keys, nothing new to set up.
- The system prompt says: search only when the answer needs live or recent facts (weather, news,
  scores, prices, opening hours), and put the source's name in the plate's `source` field.
- **At most 3 searches per question**, to keep the time and cost down.
- Long searches can come back with `stop_reason: "pause_turn"`. The board sends the conversation back
  as is to let the search finish: at most 2 times, then it shows what it has.
- Responses with search results are bigger, so the reply buffer goes from 64 KB to 512 KB, in PSRAM,
  allocated once. The timeout goes from 90 s to 120 s.

## Cost and speed

- Web search is billed **per search** by Anthropic, on top of tokens: about $10 per 1,000 searches when
  checked (2026-11). Check the current price in the Claude Console. With 3 searches at most per question, a
  question that searches costs at most about 3 cents, plus a few more input tokens.
- Questions that don't need the web don't search and cost the same as today, plus about 30 tokens for the
  time and place line.
- A question that searches will take longer: likely 5–15 s from letting go of the button to the plate.
  To measure on the board.

## What leaves the board

| What | Goes to | New? |
|---|---|---|
| The time request (SNTP) | `pool.ntp.org` | **New.** Says only that a device at your address wants the time |
| The place, the local time and zone | Anthropic, with each question | **New** |
| Web searches | Run by Anthropic, not the board. The board never contacts the sites | **New** (on Anthropic's side) |
| Questions, recordings, keys | As before (Anthropic, Groq) | No |

## Code changes

| File | Change |
|---|---|
| `main/clock.c` (new) | SNTP start after Wi-Fi, time zone from settings, `clock_now()` for the prompt, "synced" flag |
| `main/keys.c` | Two more settings, `tz` (POSIX string, 3–60 chars) and `place` (1–80 chars, one line) |
| `main/claude.c` | The time/place line, the web search tool, `pause_turn` resume, 512 KB buffer, 120 s timeout |
| `main/main.c` | `key status` lists the two new settings |
| `main/setup_web.c`, `setup_page.html` | Time zone and place on the phone page |
| `tools/setup.html` | Time zone and place on the USB page |
| `ui/ui.c` | The ring's clock reads a time callback from the platform: the board's clock on the device, the Mac's in the simulator, `--:--` until synced |

## Checks

**Before (no board needed):**
- Builds with no warnings; the simulator shows the Mac's real time.
- No log line prints the time zone or the place. All writes still go through `keys.c`, namespace
  `tessera`.
- The request JSON has the web search tool and the time line (checked in a host test, not by printing it
  on the board).
- The time zone table on the pages converts a few known zones correctly (New York, Los Angeles, London,
  Kolkata, Tokyo).

**On the board:** app slot only, backup ready.
1. After start-up, the ring shows the right local time (after you set the time zone).
2. "What time is it?" and "What's today's date?": correct, no search.
3. "What's the weather here right now?" with a place set: a plate with the weather and a source. The
   log shows the search count and the times.
4. A question that needs no search: no search, about the same speed as before.

**Stop rule unchanged:** any buzz, any sound from the speaker, or a mismatched pin means hold POWER and
restore.

## Build audit (2026-11-22)

- Builds with no warnings (firmware and simulator). App 1.82 MB (`0x1d1b00`), in the 8 MB `main_app` slot
  (the usual `vibe_mode` size warning).
- **Request check on the Mac:** the board's actual `build_request` function, compiled on the Mac with
  stand-ins for the clock and settings, produces:
  - `tools`: `show_plate`, and `{"type": "web_search_20260209", "name": "web_search", "max_uses": 3}`.
  - The first line of the instructions: `Now: Sunday 22 November 2026, 14:05 (America/New_York). The user is in
    Example City, Region.` Without a place, it says the place isn't set and to ask for it.
  - The search guidance in the prompt, and a single user message.
- **Time zone table:** each of its 51 zones, checked against the Mac's time zone database, gives the same
  local time weekly from today to November 2028, across every daylight-saving change. The check found one
  that had gone stale: **Vancouver** is on UTC−7 all year since November 2026, so it's `MST7` now, not the
  usual Pacific rule.
- **Both setup pages:** the new fields work. On a phone set to New York time, "Use this phone's zone"
  fills in `EST5EDT,M3.2.0,M11.1.0` and saves `America/New_York|EST5EDT,M3.2.0,M11.1.0`. Both pages pass
  their earlier tests (no network access, no sideways scroll, Disconnect releases the port).
- **Logs:** the time zone and place values are never logged: `clock: time zone set / not set (UTC)`,
  and status lines with lengths only. As before, the question and the plate are logged, so if a question
  or an answer mentions a place, that text is in the USB log.
- **Storage:** writes still only in `keys.c`, namespace `tessera`. ESP-Claw's saved `time_timezone` is
  the read-only fallback for the time zone.
- **SNTP** re-checks the time every hour (`CONFIG_LWIP_SNTP_UPDATE_DELAY`).

**Corrections to the plan:**
- **The certificate check doesn't use the clock.** The board's TLS doesn't check certificate dates
  (`MBEDTLS_HAVE_TIME_DATE` is off), so a wrong time from SNTP can't break or weaken HTTPS. It only gives
  a wrong clock and a wrong "today". Checking expiry dates is possible now that there's a clock, but then a
  failed or spoofed time sync would stop every request. Left off for now.
- **The pages show lengths, not the values,** for the time zone and place too. The USB page only sees what
  the board prints, and the board never prints them, so both pages behave the same way. You see what you
  type before you save it, as with the keys.
- The board still waits up to 10 s for the first time sync before its first question, so Claude gets the
  date.

## On the board (2026-11-22)

Flashed (app slot only), verified, settings intact. Before setting a zone, the time zone came from
ESP-Claw's saved default (5 characters), so the clock could be off. The owner then set the time zone and
place from the phone setup page (`place saved (10 chars)`, `time zone saved (51 chars)`), and the clock
showed the right local time.

| Question (as Groq heard it) | Web searches | Claude | Release to plate | Input tokens | Result |
|---|---|---|---|---|---|
| "What is the time right now and what is the temperature?" | 1 | 9.9 s | 11.8 s | 22,270 | The time, plus today's forecast (high, rain this afternoon). "No live temp found": it didn't make one up |
| "What place am I in right now?" | 0 | 2.0 s | 3.6 s | 6,724 | The place that was set |

**Follow-ups found on the board:**
1. **Current weather isn't found.** One search returned the forecast but not the current temperature.
   Options: add Anthropic's `web_fetch_20260209` tool so Claude can open a weather page, and/or tell it
   to look for current conditions specifically.
2. **Every question costs more.** With the web search tool in the request, even a question that doesn't
   search uses about 6,700 input tokens, against about 1,000 before. That's the tool's own instructions,
   sent with every request. A search question used 22,000. **Prompt caching** would bill the unchanging
   part (system prompt and tools) at a fraction of the price after the first question. That means moving
   the time line, which changes every minute, after the cache breakpoint.

## Change after the first flash: prompt caching and web fetch (approved 2026-11-22)

**1. Prompt caching (the cost).**
- The request is reordered so everything that never changes comes first: the tools (`show_plate`, web
  search, web fetch), then the fixed instructions as a `system` block carrying
  `"cache_control": {"type": "ephemeral"}`. Tools come before the system prompt, so that marker covers
  both, including the web tools' own instructions.
- What changes per question goes **after** the marker, so it doesn't break the cache: the time line and
  the place, in a second `system` block, then the question.
- **Lifetime: 5 minutes.** Questions are sporadic but come in bursts. A cache read costs 5% of the normal
  input price on Claude Opus 5.5. A write costs 1.25×, so the worst case (every question more than 5
  minutes after the last) is 25% more on the fixed part. A 1-hour cache would make the worst case 2×.
- The log adds `cache read N / written N` from `usage`, so the board check shows whether it works.

**2. Web fetch (current weather).**
- Add Anthropic's `{"type": "web_fetch_20260209", "name": "web_fetch", "max_uses": 1,
  "max_content_tokens": 6000}`. It can only open a page that already appeared in the conversation (a
  search result), and only one per question, with the page's text capped at about 6,000 tokens.
- The prompt adds: for current conditions (weather now, live scores, prices now), open the most relevant
  result with `web_fetch` if the search snippets don't have the current figure; never present a forecast
  as the current value.
- The board still never visits a site: Anthropic fetches the page.

**Measure on the board:** input tokens and cache numbers for two questions in a row without a search,
and the time, tokens and result for "what's the temperature here right now?".

### Build audit, second build (2026-11-22)

- Builds with no warnings. App `0x1d1e30`.
- **Request check on the Mac** (the board's `build_request`, compiled with stand-ins):
  - `tools`: `show_plate`, `web_search` (3 uses at most), `web_fetch` (1 use, 6,000 tokens).
  - `system`: two blocks. The fixed instructions carry `cache_control`; the second block has the time and
    the place.
  - For two different questions a minute apart, the request is **byte-for-byte identical up to the cache
    marker** (the first 2,772 bytes). The time and the place come only after it.
- Logs and storage unchanged from the first build. The new log fields are counts only: cache read and
  written, searches, fetches.

### On the board, after the change (2026-11-22)

Flashed (app slot only), verified. Three spoken questions, within a minute:

| Question | Release to plate | Searches / pages | Input tokens |
|---|---|---|---|
| "What time is it?" | 3.8 s | 0 / 0 | 140, plus 7,203 **written** to the cache |
| "What day is it today?" | 2.8 s | 0 / 0 | 129, plus 7,203 **read** from the cache |
| Current temperature here | 35.7 s | 2 / 1 | 151, plus 91,180 read from the cache and 4,145 written |

- **Caching works.** After the first question, the fixed 7,200 tokens are read from the cache at 5% of
  the input price, so a question without a search costs roughly what it did before web search.
- **Current weather still isn't reliable.** Two searches and one fetched page took 35 s and found no
  single current figure: the sites disagreed. Claude said so and pointed to a weather service instead of
  guessing. Web search is the wrong tool for live readings. Next: a weather tool backed by a weather API
  (for example Open-Meteo: free, no key), in its own issue and review.
