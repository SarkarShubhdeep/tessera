# Review 07: setup from a phone, through a private hotspot

**Status:** draft 2026-11-22, waiting for approval. Nothing built yet.
**Build:** step 06 plus phone setup. The board can open its own short-lived Wi-Fi hotspot, shown as a QR
code on its screen, so a phone can set Wi-Fi and keys without a computer. Issue
[#17](https://github.com/SarkarShubhdeep/tessera/issues/17). The USB setup page
([review 06](review-06-settings.md)) stays, and stays the most private option.

## Pins

**No new pins.** The hotspot uses the same Wi-Fi radio inside the chip. The screen, touch, microphone and
the pins from reviews 01–03 are unchanged.

## Why not ESP-Claw's way

ESP-Claw serves its setup page on the **home network** (`http://esp-claw.local`), over plain HTTP, with no
login, and runs its own hotspot **open by default**. Its docs say it "assumes a trusted environment". On a
shared network, anyone on the Wi-Fi could open that page and read or change the keys, or watch them go by.
Tessera does none of that:

| | ESP-Claw | Tessera (this review) |
|---|---|---|
| Where the page is | Home network, always on | Its own hotspot, only while you're setting up |
| Who can reach it | Anyone on the home Wi-Fi | Only a device that joined with the password on the screen, and has the one-time link |
| Hotspot password | None by default | Random, new every time, shown only on the screen |
| How long | Always | Until you save or cancel, at most 5 minutes |

## How it works

1. **You open it on the board.** A long press (2 s) on the ring screen shows a **Setup** plate. Its
   **Start** choice starts setup; nothing radio-related happens before that. *(Open question for the owner:
   is a long press on the ring the right gesture?)*
2. **The board starts a private hotspot** next to its normal Wi-Fi connection (the radio's
   station + access-point mode). The board stays connected to your home network while setup runs.
   - Name: `Tessera-XXXX` (the last four hex digits of the board's MAC address).
   - Security: WPA2/WPA3 mixed mode. Phones that support WPA3 use it.
   - Password: 16 random characters from the chip's hardware random number generator. New every time,
     never saved, never logged.
   - At most **one** device can join.
3. **QR code 1** on the plate is the hotspot as a standard Wi-Fi QR code. Pointing a phone camera at it
   offers to join.
4. When the phone has joined, the plate switches to **QR code 2**: `http://192.168.4.1/s/<token>`. The
   token is 128 random bits, new every time. Any other address on the board's web server answers "not
   found".
5. **The page** looks and works like the USB setup page: Wi-Fi name, Wi-Fi password, Claude key, Groq key,
   with Show and Save. Status shows "saved in tessera / from ESP-Claw / missing" and lengths, never
   values.
6. **It ends** when you tap **Done** on the page or **Cancel** on the board, or after 5 minutes. Then the
   web server stops, the hotspot goes off, the password and token are wiped from RAM, and the board is back
   to normal Wi-Fi only.

## What protects what

| Threat | What stops it |
|---|---|
| Someone on your **home** Wi-Fi opening the page | The web server answers only on the hotspot's address. A request that arrives from the home network is dropped before it's read, even with the right link |
| Someone **nearby** joining the hotspot | They need the password, which exists only on the screen and only for that session. A 16-character random password can't be guessed or cracked from a captured handshake in 5 minutes (or in years) |
| Someone listening to the radio | WPA2/WPA3 encrypts everything between the phone and the board with that password |
| A fake "Tessera-XXXX" hotspot | Your phone joins using the password from the QR code; a fake one doesn't have it, so the phone won't join it without you typing something |
| The hotspot left on | It switches off after 5 minutes whatever happens |
| Keys in the log | As in review 06: names, sources and lengths only |

**What it doesn't protect against** (this applies to the USB page too):
- Anyone who can see the screen during setup can join the hotspot. Set up where the screen is yours.
- The page inside the hotspot is plain HTTP, not HTTPS. A board can't get a certificate a phone would
  trust, so HTTPS would mean a browser warning you'd have to click through. The radio encryption covers
  that link instead, and only your phone is on it.
- **The board's flash isn't encrypted.** Anyone who has the board in their hands and a USB cable can read
  the keys out of its storage with `esptool`. Encrypting flash is possible, but it permanently changes the
  chip (one-time eFuses) and makes recovery harder. It's a separate decision with its own issue.

## The web server

- ESP-IDF's `esp_http_server`, started only during setup, stopped after. Port 80, at most 2 open
  connections, small request limits (a body over 1 KB is refused).
- Every request checks its **local address** (the address it arrived on) is the hotspot's `192.168.4.1`,
  and its path starts with `/s/<token>`. Anything else is closed without an answer.
- The page is built into the firmware. Its Content Security Policy allows only its own inline script and
  style and requests back to the board (`connect-src 'self'`), nothing else.
- Saving sends JSON (`{"setting": "claude", "value": "…"}`) and goes through the same `keys_set` checks as
  the console. The reply is the same "saved (N chars)" text.

## Screen

- A **Setup** plate with a QR code (LVGL's built-in `lv_qrcode`, turned on in the config), a one-line
  instruction ("Scan to join Tessera's setup hotspot"), the time left, and **Cancel**.
- The QR code is drawn black on white inside the plate, because phone cameras read that most reliably. It's
  the one place on the screen that isn't black.
- Not in the simulator's demo deck; the simulator gets a snapshot of the plate with a made-up code.

## What's logged

`setup: hotspot on (Tessera-XXXX, 5 min)`, `setup: a phone joined`, `setup: page opened`, each save as in
review 06, `setup: hotspot off (done / cancelled / timed out)`. Never the password, the token, or any
value.

## Code changes

| File | Change |
|---|---|
| `main/setup_ap.c` (new) | Hotspot on/off (station + access point mode), random password and token, 5-minute timer |
| `main/setup_web.c` (new) | The web server, address and token checks, the page, save handler (calls `keys_set`, then `net_apply`) |
| `main/setup_page.html` (new) | The phone page, embedded in the firmware |
| `main/net.c` | Allow switching between station and station + access point without dropping the home connection |
| `ui/plates.c`, `ui/ui.c` | The Setup plate with a QR code; long press on the ring opens it |
| `sdkconfig.defaults` | `CONFIG_LV_USE_QRCODE=y`, HTTP server limits |

## Checks

**Before (no board needed):**
- Build with no warnings; the simulator builds and shows the Setup plate.
- The web server starts only from the Setup plate and is stopped on every exit path (done, cancel, time-out).
- The address check: a request on any address other than `192.168.4.1` is closed unread.
- No log line contains the password, the token or a value.
- All writes still go through `keys.c`, namespace `tessera` only.

**On the board:** app slot only, backup ready.
1. Open the Setup plate, start setup. A phone joins from QR code 1; QR code 2 opens the page.
2. From the Mac on the **home** network, the board's home IP with the right link: no answer.
3. Save fake values from the phone, then clear them. The board stays on the home Wi-Fi throughout.
4. Leave it alone: the hotspot is gone after 5 minutes. Cancel also turns it off at once.

**Stop rule unchanged:** any buzz, any sound from the speaker, or a mismatched pin means hold POWER and
restore.
