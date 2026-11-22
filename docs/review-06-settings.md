# Review 06: Wi-Fi and the Claude key from Tessera's console

**Status:** draft 2026-11-22, waiting for approval. Nothing built yet.
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
| `wifi password` | The **next line** you type is saved as the password (8–63 characters). An empty line cancels. After 60 s it gives up | `type the wifi password on the next line`, then `wifi password saved (N chars)` |
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
