# Review 05: voice (speech to Claude through Groq)

**Status:** approved 2026-11-22, flashed (app slot only) and working (results at the end).
**Build:** step 05a plus voice. Hold the AI button, speak, let go: the board sends the recording to Groq,
gets the words back, sends them to Claude, and draws the plate. No Mac involved. The plan and the reasons
for Groq are in issue [#7](https://github.com/SarkarShubhdeep/tessera/issues/7) and
[decision 009](decisions.md).

## Pins

**No new pins.** Recording uses the microphone exactly as set up in [review 03](pin-review-03-mic.md)
(I2S receive-only, ES8311 ADC only, amplifier pin GPIO45 low). Nothing plays sound in this step either.

## What happens when you press the button

| Step | What you see | What the board does |
|---|---|---|
| Hold the AI button | Ring listens, spikes follow your voice | Copies the microphone samples into a buffer in PSRAM (16 kHz, mono, 16-bit) |
| Keep holding past 30 s | Ring switches to thinking | Recording stops by itself at 30 s (960 KB) |
| Let go | Ring thinks | Wraps the recording as a WAV file and sends it to Groq |
| Groq answers | Your words appear in small text under the ring | Sends the words to Claude as the question, same as a typed one |
| Claude answers | The plate | Same as today |

**When nothing gets sent:**
- A press shorter than 0.4 s (a tap by mistake).
- A recording where nothing was louder than the quiet-room level (−47 dBFS). The ring goes back to idle
  and a small "didn't hear anything" appears under it.
- No Groq key saved, no Wi-Fi, or a question still in flight. An answer plate says which.

The fake "thinking, then speaking" timer that runs after a release today goes away; the ring now follows
the real request.

## The Groq key

### Entering it

Typed by you, into your own Terminal, on the board's USB console. Not through Claude Code.

```
screen /dev/cu.usbmodem2101 115200
```

Then type `key groq ` followed by the key and press Enter. The console doesn't echo what you type, so the
key isn't shown on screen and isn't in the Terminal's scrollback. Type `key status` first to confirm
nothing you type appears. Leave `screen` with Ctrl-A then K.

| Command | What it does | What the board prints |
|---|---|---|
| `key groq <key>` | Saves the key, if it starts with `gsk_` and is 20–200 characters | `groq key saved (56 chars)` |
| `key groq clear` | Deletes the key | `groq key cleared` |
| `key status` | Says whether a key is saved | `groq key: saved (56 chars)` or `missing` |

A line starting with `key ` is never sent to Claude or Groq and never logged as text, even if it's
mistyped. Any other line is still a typed question, as today.

### Where it's stored: Tessera's first write to flash

Up to now Tessera has only **read** settings (review 04). The Groq key is the first thing it **writes**,
so this is the part to check most carefully.

| | |
|---|---|
| Where | The same settings partition ESP-Claw uses (`nvs` at 0x200000, 24 KB), in Tessera's own namespace `tessera`. ESP-Claw never reads that namespace |
| ESP-Claw's settings | Namespace `app` stays opened read-only. No code path writes or erases it |
| Erasing | Still never: no `nvs_flash_erase`, no `nvs_erase_all`. `key groq clear` removes only the one entry (`nvs_erase_key` on `groq_key` in `tessera`) |
| Size | One string of about 60 bytes, a few of NVS's 32-byte entries. The boot log prints the partition's used and free entries before and after, so we can see the write is that small |
| Wear | One write per `key groq` command. Nothing writes at boot or during normal use |

Going back to ESP-Claw later: its settings are as it left them. The `tessera` namespace stays behind
unless you clear the key first (`key groq clear`); it's harmless to ESP-Claw either way.

### Where it goes

Only in the `Authorization: Bearer` header to `https://api.groq.com/openai/v1/audio/transcriptions`,
over HTTPS. The log says whether a key is saved and how long it is, never the key.

## The request to Groq

`POST https://api.groq.com/openai/v1/audio/transcriptions`, `multipart/form-data`:

| Field | Value |
|---|---|
| `file` | `voice.wav`: the recording with a 44-byte WAV header, 16 kHz mono 16-bit |
| `model` | `whisper-large-v3-turbo` |
| `response_format` | `json` (just `{"text": "..."}`) |
| `temperature` | `0` |
| language | Not set: Whisper detects it |

**Certificates:** `api.groq.com` sends the same chain as `api.anthropic.com` (checked today: WE1, then
GTS Root R4), so the same four pinned GTS roots are used. No new certificates.

**Speed (to measure on the board):** a 5 s question is 160 KB to upload. At the −71 dBm we measured,
expect roughly 0.5–1.5 s for Groq, plus the 2–3 s Claude already takes. The log prints each part:
recording length, upload + transcription time, Claude time, and the total from release to plate.

**Free-tier limits:** about 2,000 requests and 8 hours of audio a day; each request counts as at least
10 s. A rejected request (HTTP 429) becomes an answer plate saying "Groq limit reached".

## What leaves the device

| What | Goes to | When |
|---|---|---|
| The recording | Groq only | Only after you let go of the button, never while idle |
| The words Groq returns | Anthropic, as the question | Right after |
| The Groq key | Groq only, in the header | With each recording |
| The Claude key | Anthropic only | As today |

The recording lives only in RAM. It's overwritten by the next one and gone at power-off; it's never
written to flash. The transcript is printed in the USB log (like the plate already is), which only your
Mac sees.

## Code changes

| File | Change |
|---|---|
| `main/mic.c` | Recording on/off; while on, copies the left-channel samples into a 960 KB PSRAM buffer allocated once at start. Tracks the loudest level of the recording |
| `main/keys.c` (new) | The `tessera` namespace: save, clear and read the Groq key. The only file that writes NVS |
| `main/groq.c` (new) | WAV header, multipart upload, parses `text`. Runs in the same worker task as Claude, so only one request is in flight |
| `main/claude.c` | New entry point that takes a recording: Groq first, then the question to Claude |
| `main/main.c` | Button flow above; `key` commands on the console; the transcript under the ring |
| `ui/ui.c` | A one-line caption under the ring (simulator too), `ui_set_caption()` |

## Checks

**Before (no board needed):**
- Build with no warnings; the simulator still builds and shows the caption.
- Every `nvs_set_*`, `nvs_commit` and `nvs_erase_*` call is in `keys.c` and uses the `tessera` namespace;
  `app` is still opened only `NVS_READONLY`; no `nvs_flash_erase` or `nvs_erase_all` anywhere.
- No log line can print either key; a `key` line is never logged or sent anywhere.
- The Groq key is used only in the Groq request header.
- The pin dump matches review 03 (no new pins).

**On the board:** app slot only, backup ready, as before.
1. Boot: `groq key: missing`, NVS free-entry count printed.
2. You enter the key in your own Terminal. Log: `groq key saved (N chars)`, used entries up by a few.
3. Reboot: `groq key: saved`, ESP-Claw's settings still read as before.
4. Short tap: nothing sent. Silent hold: "didn't hear anything".
5. Ask a few questions out loud, including one in another language. Note release-to-plate time.

**Stop rule unchanged:** any buzz, any sound from the speaker, or a mismatched pin means hold POWER and
restore.

## Build audit (2026-11-22)

- Builds with no warnings. App 1.83 MB (`0x1beca0`), in the 8 MB `main_app` slot. The size check still
  warns about the factory's 1.75 MB `vibe_mode` slot; nothing is written there. The simulator builds and
  shows the caption ([design/gallery/orb-thinking-heard.png](../design/gallery/orb-thinking-heard.png)).
- **NVS:** `nvs_set_str`, `nvs_commit` and `nvs_erase_key` appear only in `main/keys.c`, all on namespace
  `tessera`, all reached only from a `key groq` command. `app` is opened once, `NVS_READONLY`
  (`settings.c`). Reading the Groq key opens `tessera` read-only too, so a board with no key saved gets
  nothing created at boot. `nvs_flash_erase` and `nvs_erase_all` are not in the firmware at all (checked
  the symbols in `tessera.elf`); `nvs_erase_key` is the only erase linked.
- **Keys and logs:** every log line was checked. Key lines print only "saved / missing / cleared" and a
  length. A rejected key (HTTP 401/403) logs "the saved Groq key was rejected" without Groq's message; other
  Groq errors print their message only if it has no `gsk_` in it. A console line that contains `gsk_`
  without `key groq` in front is refused and never sent to Claude. The console line buffer is wiped after
  each line. Maximum log level is INFO, so the HTTP client's header dumps aren't compiled in.
- **Where the Groq key goes:** read from NVS for each recording, put into the `Authorization` header to
  `https://api.groq.com/openai/v1/audio/transcriptions` (`groq.c`), and the local copies are wiped right
  after.
- **Echo:** the USB console is TinyUSB's CDC console, which doesn't echo input (only its test apps do).
  The board check still starts with `key status` to confirm.
- **Pins:** no new pin calls. The I2S channel is still receive-only (`dout` unused); the codec's GPIO
  helper still isn't linked.
- **Known limit:** the caption font is Latin-only. A question in, say, Hindi still goes to Claude
  correctly, but the caption under the ring shows only the Latin characters in it.

## On the board (2026-11-22)

**It works: Shubh talks to Tessera, with no Mac involved.** Flash verified, the board restarted by itself.
`key status` answered `groq key: missing` and the command didn't come back on screen (no echo). Shubh
entered the key in their own Terminal; it was never seen by Claude Code.

| Question (as Groq heard it) | Audio | Groq | Claude | Release to plate | Plate |
|---|---|---|---|---|---|
| "What is the temperature in [a city] right now?" | 3.8 s | 9.6 s | 3.3 s | 12.9 s | answer (no live data) |
| "What are your features? Can you give me top two of them to try out now?" | 6.4 s | 2.5 s | 2.3 s | 4.9 s | list |

- The first request after boot was slow at Groq (9.6 s); the next was 2.5 s. Likely the first TLS
  connection and Groq's own cold start; worth measuring again over a few more questions.
- Transcription was exact both times. Speech peaked at −18 to −21 dBFS.
- The room read −40 to −43 dBFS today (−50 last time), above the −47 silence level, so a silent hold
  would still be sent. To revisit if Whisper invents words from silence.
- Claude has no live data yet (weather, time): it says so. A follow-up, not part of this step.
