# Setup

Everything Tessera uses today, and how to change each part. The reasons behind each choice are in the
[decision log](decisions.md); how each step was checked on the board is in the reviews
([01](pin-review-01-screen.md), [02](pin-review-02-input.md), [03](pin-review-03-mic.md),
[04](review-04-claude.md), [05](review-05-voice.md)).

## At a glance

| Part | Tessera uses | To change it |
|---|---|---|
| Board | ESP-Mosaico v1.2 (ESP32-S31, 480×480 AMOLED, touch, mic, AI button) | Pins and drivers live in `firmware/main/board.c` and `board.h`. A second board is on the roadmap |
| Firmware | C, ESP-IDF 6.1, LVGL 9.5 | LVGL is pinned in `third_party/lvgl`; ESP-IDF version in your install |
| Simulator | The same UI code in an SDL2 window on macOS | `sim/`, see below |
| Answers | Claude API, `claude-opus-5-5`, effort `low`, with web search | `MODEL` and the `effort` line in `firmware/main/claude.c` |
| Speech to text | Groq, `whisper-large-v3-turbo` | `URL` and `MODEL` in `firmware/main/groq.c` |
| Keys and Wi-Fi | On the device, in its settings storage (NVS) | See [Keys and settings](#keys-and-settings) |

## Board

ESP-Mosaico v1.2 from Espressif. Pin map, buttons and recovery: [hardware notes](hardware.md).

Everything that touches a pin is in `firmware/main/board.c` (and the microphone in `mic.c`). Before any
change that drives a new pin, write a pin review like the ones above: list every output pin and its level
at boot. The motor on GPIO8 once caused a reset loop ([hardware notes](hardware.md#lesson-the-motor-at-boot)).

## Firmware toolchain

- **ESP-IDF 6.1.** The ESP32-S31 is a preview target, so every `idf.py` command needs `--preview`.
- **LVGL 9.5**, pinned as a submodule in `third_party/lvgl`. The UI in `ui/` uses only LVGL, so it builds
  for the board and the Mac unchanged.
- **C.** Why not Rust: [decision 001](decisions.md#001-c--lvgl-not-rust-2026-11-21).

```bash
. ~/esp/esp-idf/export.sh
cd firmware
idf.py --preview set-target esp32s31   # once
idf.py --preview build
```

## Simulator

The 480×480 screen in a window on a Mac. Needs Homebrew.

```bash
brew install sdl2 cmake ninja
cd sim
cmake -S . -B build -G Ninja && ninja -C build
./build/tessera_sim
```

Details, snapshots and the plate files: [sim/README.md](../sim/README.md).

## Models and services

### Answers: Claude

The device calls the [Claude Messages API](https://docs.claude.com) over HTTPS
(`firmware/main/claude.c`): model `claude-opus-5-5`, effort `low` (short answers, quick replies),
server-side fallback on, and one tool, `show_plate`, through which Claude picks and fills in a plate.

- **Another Claude model:** change `MODEL`. Effort is the `output_config.effort` line in
  `build_request`.
- **The system prompt and the plate tool** are `SYSTEM_PROMPT` and `TOOL_JSON` in the same file; the
  plate types themselves are in the [design system](design-system.md).

Tessera is Claude only ([decision 003](decisions.md#003-claude-only-keep-mcp-2026-11-21)).

### Live data: the clock, your place, web search

- **Clock:** the board sets its time over the internet (SNTP, `pool.ntp.org`) once Wi-Fi is up. Set a
  **time zone** on a setup page, otherwise the clock is UTC or ESP-Claw's saved zone.
- **Place:** optional, a city and region. Each question tells Claude the local time and, if set, the place,
  so "the weather here" means something. The place goes to Anthropic with each question; it's never logged.
- **Web search:** Claude can use Anthropic's server-side web search (`web_search_20260209`), at most 3
  searches per question, only when an answer needs live or recent facts. Anthropic runs the search; the board
  never visits a website. To turn it off or change the limit: `SEARCHES` and the tool in `claude.c`.
- **Cost and speed** (measured 2026-11-22):
  - A question without a search: about 2.8–3.8 s from letting go to the plate.
  - The fixed part of the request (instructions and tools, about 7,200 tokens) is cached by Anthropic for
    5 minutes. A question within 5 minutes of the last one reads it at 5% of the input price. The first
    question after a gap writes it at 1.25×.
  - A question that searches takes longer (10–35 s) and costs more: each search is billed, plus the
    results it reads.
- **Not reliable yet: live readings.** Web search finds forecasts and news well, but not a single current
  temperature. A weather tool backed by a weather service is planned.

### Speech to text: Groq

Claude's API takes text, images and documents, not audio, so the recording goes to a speech-to-text
service first ([decision 009](decisions.md#009-speech-to-text-through-groqs-hosted-whisper-2026-11-22)).
Tessera uses Groq's hosted `whisper-large-v3-turbo`: about 2.5 s for a short question on the Mosaico, and
a free tier of about 2,000 requests a day (checked November 2026).

The request (`firmware/main/groq.c`) is the OpenAI-style `audio/transcriptions` form: a 16 kHz mono WAV
file, the model name, `response_format=json`.

- **Another service with the same form:** change `URL` and `MODEL`, and check that the server's
  certificate chain ends at one of the roots in `firmware/main/certs/anthropic_roots.pem` (Google Trust
  Services). If not, add its root to that file.
- **Cloudflare Workers AI or a self-hosted whisper.cpp server:** both run Whisper, but take a different
  request. That means changing the request in `groq.c`, not just the two lines.

## Keys and settings

All on the device, in its settings storage (the `nvs` partition). Nothing is in the repo, and the log
never prints a key, a password or the network name.

| Setting | Where it lives |
|---|---|
| Wi-Fi name and password | Tessera's namespace `tessera`, else ESP-Claw's `app` |
| Claude API key | Tessera's namespace `tessera`, else ESP-Claw's `app` |
| Groq API key | Tessera's namespace `tessera` |

For each setting, Tessera uses its own saved value if there is one, and otherwise what the factory
ESP-Claw firmware saved, which it reads but never changes. So a board that ran ESP-Claw works with
nothing entered, and a fresh board can be set up from scratch.

### The setup page

Enter settings with **[tools/setup.html](../tools/setup.html)**, a single page you open on your Mac in
**Chrome or Edge** (Safari and Firefox can't talk to USB devices):

1. Plug the board in, and close `screen` or anything else using its port.
2. Open `tools/setup.html` in Chrome and click **Connect board**. Pick the Mosaico.
3. Type a value. **Show** lets you check it; the page warns if it doesn't look right.
4. **Save.** The field empties, and the setting shows "saved in tessera" with its length.

**Clear** buttons remove Tessera's value, so the board falls back to ESP-Claw's, if any. Wi-Fi reconnects
by itself after a change, and a new Claude key is used from the next question on.

What the page does with your values:

- It talks to the board only over the USB cable (Web Serial). Nothing goes over Wi-Fi or the internet.
- It can't load or send anything on the network: its Content Security Policy blocks every request. It's
  one file, so you can read all of it.
- It never stores a value, and the board's replies never contain one: only "saved", "missing" or
  "cleared" and a length.

### From a phone

No computer needed. The board opens its own private hotspot for a few minutes:

1. On the ring screen, **hold your finger on the screen for 10 seconds**. The Setup plate appears. Tap
   **Start**.
2. **Scan the first QR code** with your phone's camera and join `Tessera-XXXX`. Your phone may say the
   network has no internet. Stay connected.
3. **Scan the second QR code**. It opens the setup page from the board.
4. Save what you want to change, then tap **Done**. Wi-Fi changes take effect then.

What keeps it private:

- The hotspot password is random, new every time, and shown only on the board's screen. Only one device
  can join, and the hotspot switches itself off after 5 minutes.
- The page answers only on the hotspot. From the home network, even someone with the link gets nothing:
  the connection is closed before anything is read.
- Anyone who can see the screen during those minutes can join, so set up where the screen is yours.

### The console, underneath

The page sends plain text commands over the USB console. You can type them yourself in a serial terminal
(`screen /dev/cu.usbmodem2101 115200`), but the page is easier: the console doesn't echo what you type,
and the log keeps scrolling.

| Command | What it does |
|---|---|
| `key status` | Each setting: saved in Tessera, from ESP-Claw, or missing, and its length |
| `wifi name <name>` | Saves the network name and reconnects |
| `wifi password` | The **next line** is the password; an empty line cancels |
| `wifi clear` | Deletes Tessera's Wi-Fi name and password |
| `key claude <key>`, `key groq <key>` | Saves that key |
| `key claude clear`, `key groq clear` | Deletes that key |

The console only takes these commands. Anything else is refused without being printed, and **nothing typed
into it is ever sent to Claude**: questions come from the AI button only.

## Flashing and recovery

**Back up first.** Before the first flash, save the whole flash (16 MB) and keep it **out of the repo**:
it contains the saved Wi-Fi password and API keys.

```bash
esptool --chip esp32s31 -p /dev/cu.usbmodem2101 read-flash 0 0x1000000 mosaico-full.bin
```

**Flash only the app slot.** Tessera keeps the factory partition table and writes its app to the
`main_app` slot at `0x210000`. The bootloader (at `0x2000`) is written once, from the same ESP-IDF as the
app; the factory bootloader can't start an ESP-IDF 6.1 release app
([hardware notes](hardware.md#lesson-the-factory-bootloader)).

Running `esptool` once switches the board to download mode, and it comes back on the same port. Then:

```bash
esptool --chip esp32s31 -p /dev/cu.usbmodem2101 chip-id
esptool --chip esp32s31 -p /dev/cu.usbmodem2101 --before no-reset --after watchdog-reset \
  write-flash 0x210000 firmware/build/tessera.bin
```

The board restarts by itself afterwards. Don't use `idf.py flash`: it writes more than the app slot.

**If it goes wrong** (no screen, a reset loop, the motor buzzing): hold POWER to switch off, then hold
BOOT while tapping POWER to enter download mode, and write back the backup with
`--after no-reset write-flash 0 mosaico-full.bin`. The [hardware notes](hardware.md#recovery) have the
details.
