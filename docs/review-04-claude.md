# Review 04: Wi-Fi and Claude (text first)

**Status:** approved 2026-11-22, flashed (app slot only) and working (results at the end).
**Build:** step 03c plus the network. A question typed into the USB console goes to Claude; Claude
answers by choosing a plate, and the device draws it. Voice comes in a later step: the Claude API takes
text, images and documents, not audio, so speech needs its own speech-to-text step first.

## Pins

**No new pins.** The ESP32-S31's Wi-Fi 6 radio is inside the chip. Every pin from reviews
[01](pin-review-01-screen.md), [02](pin-review-02-input.md) and [03](pin-review-03-mic.md) stays as it is.

## Your saved settings stay read-only

The factory ESP-Claw firmware saved the Wi-Fi network and the Claude API key in the device's settings
storage (NVS, the `nvs` partition, namespace `app`). Tessera reads them there, on the device. The key
names come from ESP-Claw's source and match the strings in the factory app:
`wifi_ssid`, `wifi_password`, `llm_api_key`, `llm_backend`, `llm_model`, `llm_base_url`.

| Risk | What this build does |
|---|---|
| Overwriting or erasing the settings | The namespace is opened **read-only**. The code never calls `nvs_flash_erase`; if the storage needs repair, Tessera stops and reports it instead of erasing |
| The Wi-Fi driver saving its own copy | Wi-Fi settings are kept in RAM only (`esp_wifi_set_storage(WIFI_STORAGE_RAM)`, `CONFIG_ESP_WIFI_NVS_ENABLED=n`) |
| Radio calibration data written to NVS | Off (`CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=n`); the radio calibrates at each boot instead |

Going back to ESP-Claw later finds its settings exactly as it left them.

## Secrets never leave the device except to Anthropic

- The **API key** is sent only in the `x-api-key` header, over HTTPS (certificate-checked against the
  Google Trust Services roots, see below), to `api.anthropic.com`. If the saved key isn't a Claude key (it doesn't start with `sk-ant-`),
  Tessera doesn't send it anywhere.
- The **log** says whether each setting was found and how long it is, never its value. The Wi-Fi network
  name isn't printed either. Safe to print: the saved model name and backend type.
- Nothing is written to flash.

## The request

`POST https://api.anthropic.com/v1/messages`, `anthropic-version: 2026-10-21`:

- Model `claude-opus-5-5`, `effort: low` (short answers for a small screen, quick replies).
- Server-side fallback on (`fallbacks: "default"`, beta `server-side-fallback-2026-07-01`): if the model
  declines a request for safety reasons, the API retries it on a suitable model.
- One tool, `show_plate`: the plate types and fields from the [design system](design-system.md). The
  system prompt asks Claude to answer by calling it once.
- Each question is a new conversation (no memory yet).

**On the device:** a `show_plate` call is drawn as that plate; plain text becomes an answer plate; a
refusal or an error becomes an answer plate saying so. The ring thinks while the request is in flight.

## Checks

**Before (no board needed):** build with no warnings; check that nothing in the build writes to NVS or
erases it, and that no log line can print a secret.

**On the board:** app slot only. Boot log: each setting found or missing (no values), Wi-Fi connected,
then a test question sent from the Mac and a plate on screen. Stop rule unchanged.

## Build audit (2026-11-22)

- Builds with no warnings. App 1.84 MB (Wi-Fi and TLS), in the 8 MB `main_app` slot. The size check
  warns that it would no longer fit the factory's 1.75 MB `vibe_mode` slot; nothing is written there.
- No `nvs_set_*`, `nvs_commit`, `nvs_erase_*` or `nvs_flash_erase` anywhere in Tessera's code; the one
  `nvs_open` is `NVS_READONLY`. `CONFIG_ESP_WIFI_NVS_ENABLED` and
  `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE` are off in the built config.
- One caveat: `nvs_flash_init` itself may tidy a half-written page left by an interrupted write. That is
  ESP-IDF's own housekeeping, which ESP-Claw also runs at every boot; it never changes a stored value.
- No log line prints the key, the Wi-Fi password or the network name (checked every log call). The
  maximum compiled log level is INFO, so the HTTP client's header dumps (DEBUG) aren't in the build.
- The key is used in exactly one place: the `x-api-key` header to `https://api.anthropic.com/v1/messages`.

## On the board (2026-11-22)

**First flash: TLS failed, and the log showed the Wi-Fi network name.**
- ESP-IDF's certificate bundle rejected `api.anthropic.com`: the server sends GTS Root R4 cross-signed by
  GlobalSign Root CA, a root the bundle has dropped, and the bundle's check follows the chain to it. The
  connection now trusts the four GTS roots (`firmware/main/certs/anthropic_roots.pem`, copied from
  ESP-IDF's Mozilla list), so the chain ends at GTS Root R4.
- The audit missed one log line: ESP-IDF's Wi-Fi library prints the network name at INFO when it
  connects. Its log level is now WARN; the captured log was deleted.

**Second flash: works.** Settings found (no values printed), Wi-Fi up at −71 dBm in about 3.5 s, and the
network name appears nowhere in the log.

| Question | Reply | Tokens in / out | Plate |
|---|---|---|---|
| "What is 17 times 23?" | 2.1 s, HTTP 200, `tool_use` | 958 / 94 | number |
| "Give me a short plan for a calm Sunday morning" | 2.8 s, HTTP 200, `tool_use` | 966 / 196 | list |

Also in this build: the start screen (wordmark, version, work-in-progress mark), Wi-Fi brought up in its
own task so the screen keeps running, and the flash tool now resets the board itself after writing.
