# Review 06: Wi-Fi and the Claude key from Tessera's console

**Status:** approved 2026-11-22, built and audited (below). Not flashed yet.
**Build:** step 05b plus settings. Today Tessera can only use the Wi-Fi and Claude key that the factory
ESP-Claw firmware saved. This step lets the owner set them from Tessera's own console, the same way as
the Groq key, so a board that never ran ESP-Claw can be set up. Issue
[#12](https://github.com/SarkarShubhdeep/tessera/issues/12).

## Pins

**No new pins.** Nothing changes in the board code, the screen, the microphone or the radio setup.

## The commands

Typed by the owner, in their own terminal (`screen /dev/cu.usbmodem2101 115200`), never through an AI
agent or a chat. The console doesn't echo anything you type.

| Command | What it does | What the board prints |
|---|---|---|
| `wifi name <name>` | Saves the network name (1–32 characters, spaces allowed) | `wifi name saved (N chars)` |
| `wifi password` | The **next line** you type is saved as the password (8–63 characters). An empty line cancels | `type the wifi password on the next line`, then `wifi password saved (N chars)` |
| `wifi clear` | Deletes Tessera's saved name and password | `wifi settings cleared` |
| `key claude <key>` | Saves the Claude key, if it starts with `sk-ant-` and is 20–300 characters | `claude key saved (N chars)` |
| `key claude clear` | Deletes it | `claude key cleared` |
| `key status` | Lists every setting: saved or missing, its length, and where it comes from | see below |

`key status` after this step:

```
wifi name: saved in tessera (N chars)
wifi password: from ESP-Claw (N chars)
claude key: missing
groq key: saved in tessera (N chars)
```

The password goes on its own line so it never shares a line with a command word. While the board is
waiting for it, whatever comes next is treated as the password and nothing else.

## Where it's stored

Same place and the same rules as the Groq key ([review 05](review-05-voice.md#where-its-stored-tesseras-first-write-to-flash)):
Tessera's own namespace `tessera` in the `nvs` partition.

| Entry | Namespace | Written when |
|---|---|---|
| `wifi_ssid` | `tessera` | `wifi name …` |
| `wifi_pass` | `tessera` | the line after `wifi password` |
| `claude_key` | `tessera` | `key claude …` |
| `groq_key` | `tessera` | `key groq …` (unchanged) |

- ESP-Claw's namespace `app` stays **read-only**. Tessera never writes, changes or erases it.
- No partition or namespace erase anywhere. A `clear` command removes only its own entries
  (`nvs_erase_key`).
- Nothing is written at boot or during normal use: only when you type one of these commands.
- Three more entries of up to about 330 bytes in all. The log prints used and free entries before and
  after each save, as in review 05.

## Which value wins

For each setting on its own: **Tessera's saved value if there is one, otherwise ESP-Claw's.** So a board
that already works keeps working with nothing typed, and anything you set in Tessera takes over. Clearing
Tessera's value goes back to ESP-Claw's, if there is one.

## Applying a change without a restart

- **Wi-Fi:** after `wifi name`, `wifi password` or `wifi clear`, Tessera reconnects with the new name and
  password. If Wi-Fi wasn't started at boot (nothing saved anywhere), it starts now.
- **Claude key:** read from storage for each question, like the Groq key, so a new key is used from the
  next question on. The key is no longer kept in RAM between questions.
- If the Claude key or Wi-Fi is missing, the AI button and typed questions answer with a plate that says
  which one, and how to set it.

## Secrets

- The log prints only "saved / missing / cleared", the length and the source. Never the network name, the
  password or a key. (Review 04's rule about the network name still holds.)
- A typed line that contains `sk-ant-` or `gsk_` without a `key` command in front is refused and never
  sent to Claude, as `gsk_` is today.
- The line after `wifi password` is never logged, and never sent anywhere except to the Wi-Fi driver.
- The console's line buffer is wiped after every line, as today.

## Code changes

| File | Change |
|---|---|
| `main/keys.c` | Generalised from the Groq key to four entries, each with its own check (length, prefix). Still the only file that writes NVS. Adds the "Tessera, else ESP-Claw" lookup |
| `main/settings.c` | Unchanged: still opens `app` read-only. Its values become the fallback |
| `main/net.c` | `net_apply()`: starts Wi-Fi if needed, or disconnects and reconnects with new settings |
| `main/claude.c` | Reads the Claude key for each request instead of keeping it; plate for a missing key or Wi-Fi |
| `main/main.c` | The new commands, the password-on-the-next-line mode, the `sk-ant-` guard; bring-up no longer stops when ESP-Claw's settings are missing |

## Checks

**Before (no board needed):**
- Build with no warnings; the simulator still builds.
- Every `nvs_set_*`, `nvs_commit` and `nvs_erase_key` call is in `keys.c`, on namespace `tessera`. `app`
  is still opened only `NVS_READONLY`. No `nvs_flash_erase` or `nvs_erase_all` in the firmware (checked
  in `tessera.elf`).
- No log line can print the network name, the password or a key.
- The Claude key is used only in the `x-api-key` header to `api.anthropic.com`; the Groq key only in the
  `Authorization` header to `api.groq.com`.
- The pin dump matches review 03.

**On the board:** app slot only, backup ready, as before.
1. `key status` with nothing set in Tessera: everything "from ESP-Claw", Groq "saved in tessera". Voice
   still works.
2. You set `key claude` with the same key. `key status` shows it "saved in tessera". A question still
   gets a plate.
3. You set `wifi name` and `wifi password` to the same network. The board reconnects by itself; a
   question still gets a plate.
4. `wifi clear` and `key claude clear`: back to ESP-Claw's values, still working.
5. Restart: the same `key status` as before the restart.

**Stop rule unchanged:** any buzz, any sound from the speaker, or a mismatched pin means hold POWER and
restore.

## Build audit (2026-11-22)

- Builds with no warnings. App 1.83 MB (`0x1bf4f0`), in the 8 MB `main_app` slot (the usual warning about
  the factory's 1.75 MB `vibe_mode` slot; nothing is written there). The simulator still builds.
- **Changed from the plan:** no 60 s time-out after `wifi password`. With one, a password typed after the
  time-out would have been read as a question: logged and sent to Claude. Now the next line is always the
  password, however long it takes; an empty line cancels.
- **NVS:** `nvs_set_str`, `nvs_commit` and `nvs_erase_key` appear only in `main/keys.c`, on namespace
  `tessera`. `app` is opened only `NVS_READONLY` (in `settings.c` and in `keys.c`'s fallback read). Reads
  open `tessera` read-only too, so nothing is created at boot. `nvs_erase_key` is the only erase linked
  into `tessera.elf`; `nvs_flash_erase` and `nvs_erase_all` aren't in it.
- **Logs:** every log line with a `%s` was checked. Settings lines print only a fixed setting name, the
  source ("saved in tessera", "from ESP-Claw"), "saved / missing / cleared" and a length. Wi-Fi prints
  only where the network came from, never its name. The questions Claude is asked are still logged, and a
  line with `sk-ant-` or `gsk_` in it never becomes a question.
- **Where each secret goes:** the Wi-Fi name and password only to the Wi-Fi driver (RAM, the driver's
  flash storage stays off); the Claude key only to the `x-api-key` header to `api.anthropic.com`, read for
  each question and wiped from our buffer right after; the Groq key as before. ESP-Claw's Claude key is
  only used if it starts with `sk-ant-`.
- **Pins:** no new pin calls.

## On the board, first flash (2026-11-22)

Flashed (app slot only), verified, restarted by itself. `key status`: Wi-Fi name, Wi-Fi password and
Claude key "from ESP-Claw", Groq key "saved in tessera". So the fallback works and nothing needs to be
typed on a board that ran ESP-Claw. A typed question got a number plate in 2.6 s with the key read
per question.

**Then a key leaked into a question.** While entering settings in `screen`, a line holding the owner's
Claude key was sent to Claude as a question. The `sk-ant-` guard should have refused it; the likely cause
is a paste that arrived split over two lines, so the second part didn't contain the prefix. The key went
only to Anthropic's API over HTTPS. It also stayed in the local USB log (`claude: asking: …`). Nothing
went over the local network unencrypted. The owner will rotate the key.

The fault is the design, not the paste: any line that wasn't a command became a question, in a console
where you can't see what you type. Issue [#16](https://github.com/SarkarShubhdeep/tessera/issues/16).

## Change after the first flash: no questions from the console, and a setup page

**Firmware.**
- The console **never sends a line to Claude**, and never logs a line that isn't a command. Questions
  come from the AI button only. A non-command line gets `setup: not a command`, without echoing it.
- Replies to commands get their own log tag, `setup`, so the setup page can show only those.
- Nothing else changes: same commands, same storage rules, no new pins.

**Setup page (`tools/setup.html`).** One HTML file, opened in Chrome on the Mac.
- It talks to the board over the USB cable with Chrome's Web Serial. Nothing goes over Wi-Fi or the
  network.
- Fields for Wi-Fi name, Wi-Fi password, Claude key and Groq key, each with show/hide and Save, plus
  Refresh status and Clear buttons. Every value is checked (length, prefix, one line) before it's sent.
- It shows only the board's `setup` replies, never the rest of the log.
- **No network access:** no external files, and a Content Security Policy of `default-src 'none'` plus
  inline script and style only, so the page can't make any request. Values are never stored by the page:
  each field is emptied after Save.
- **Board reset safety:** the board resets only when the USB "RTS" signal is on while "DTR" is off. The
  page sets both on together once, right after opening the port, and never changes them, so opening or
  closing it doesn't reset the board or enter download mode.

**Checks:** build with no warnings; no `claude_ask` reachable from the console; every console reply
prints no value; the page loads with the network blocked and contains no URL other than its own text. On
the board, **fake values only** (a made-up Claude key and Wi-Fi name), then `clear` back to ESP-Claw's.

### Build audit, second build (2026-11-22)

- Builds with no warnings. App `0x1bf3b0`.
- `claude_ask` (the typed-question path) is removed from the code and isn't in `tessera.elf`. The console
  handles `key` and `wifi` commands and the line after `wifi password`; anything else gets
  `setup: not a command (N chars); nothing sent`, its only log line, which prints a length.
- Storage unchanged from the first build: writes only in `keys.c`, namespace `tessera`;
  `nvs_erase_key` is the only erase linked.
- **Setup page**, tested in Chrome against a stand-in board:
  - A request to the web from the page was blocked by its Content Security Policy, and the page made
    no network request of its own. It contains no URL.
  - What it sent: `key status`, `key claude <key>`, `wifi password` and then the password on its own line.
    A trailing newline pasted with the key was removed before sending.
  - The fields were emptied after Save, and too-short values couldn't be saved.
  - Only `setup:` replies were shown; the `mic` and `sysmon` log lines were ignored.
  - It set DTR and RTS on once, together, and never changed them.

## On the board, second flash (2026-11-22)

Flashed (app slot only), verified. Checked from the Mac with **fake values only**, then cleared:

| Step | Result |
|---|---|
| `key status` | Wi-Fi and Claude "from ESP-Claw", Groq "saved in tessera" |
| A typed question | `not a command (17 chars); nothing sent`: didn't reach Claude |
| A loose `sk-ant-…` line | `not a command (39 chars); nothing sent` |
| `key claude <fake>` | Saved; storage 355 → 358 used entries of 756 |
| `wifi name <fake>` | Saved; Wi-Fi left the real network and retried the fake one |
| `wifi password` + fake | Saved, but **the new settings were refused**: `sta is connecting, cannot set config` |
| `wifi clear`, `key claude clear` | Both cleared, `key status` back to ESP-Claw's values, but Wi-Fi was **stuck on the fake network** (same error) |

**Bug found:** the Wi-Fi driver refuses new settings while it is connecting or retrying. `net_apply` now
stops Wi-Fi, sets the new name and password, and starts it again. A restart also recovers, because boot
reads the settings fresh. Fixed in the next build, which still needs to be flashed and checked.
