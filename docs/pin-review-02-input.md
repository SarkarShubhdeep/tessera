# Pin review 02: touch, AI button, faster display link

**Status:** approved 2026-11-21, flashed (app slot only) and running (results at the end).
**Build:** step 03a plus input. You drive it with touch and the AI button; the automatic demo is gone.
Still no audio, motor, Wi-Fi or modules.

Everything in [pin review 01](pin-review-01-screen.md) stays exactly as it is (including the bootloader
hook that holds the motor low). This review lists only what changes.

## New pins

| # | When | GPIO | What | Set to | Why it's safe |
|---|---|---|---|---|---|
| 10 | After the display | **56** | On-board I2C data (SDA) | I2C master, open-drain, internal pull-up | Open-drain: the pin only ever pulls low or lets go, so it can't fight another chip. The factory firmware uses the same bus |
| 11 | After the display | **3** | On-board I2C clock (SCL) | I2C master, open-drain, internal pull-up, 400 kHz | Same |
| 12 | Touch start | **6** | Touch interrupt | **Input**, no pulls | Input only. Read by polling; no interrupt handler is installed |
| 13 | Touch start | **7** | AI button | **Input**, pull-up | Input only. The button pulls it low when pressed |

**No new outputs.** The only new pins that can drive anything are the two I2C lines, and only in the
open-drain way I2C always works.

## What goes over I2C

Only the touch chip (CST9220, address `0x5A`) is addressed: a soft reset, reading its ID, then reading
touch reports. The codec (`0x19`), IMU (`0x69`), magnetometers (`0x11`/`0x12`) and fuel gauge (`0x55`)
share the bus but are never addressed. No bus scan.

## Changed: display link 40 → 80 MHz

The QSPI clock on GPIO42 doubles, to cut the time each frame takes to send. The panel's datasheet
isn't public; ESP-Claw runs it at 40 MHz. **Worst case:** a garbled or blank image. That's not harmful,
and flashing the 40 MHz build again fixes it.

## How it behaves

- **Touch:** tap the ring to cycle its states; swipe left/right through five example plates.
- **AI button:** hold it and the ring listens; let go and it thinks for 1.5 s, speaks for 3 s, then
  rests. (A stand-in until the engine and audio exist.)

## Checks

**Before (no board needed):** build with no warnings; list every pin-touching call in our code and the
new components (`esp_lcd_touch`, `esp_lcd_touch_cst9220`, the I2C driver) and match it to this table.

**On the board:** same flash plan as before (bootloader unchanged, app slot only). The boot log prints the
pin check again, now including 3, 6, 7 and 56. Stop rule unchanged: any buzz or a mismatched pin means
hold POWER and restore.

## Build audit (2026-11-21)

- Builds with no warnings. App 909 KB (`0xe3170`), 49% of the slot free. Bootloader unchanged.
- `main/board.c` is still the only file of ours that names a pin; the new ones are 56/3 (I2C bus),
  6 (touch INT, via the driver) and 7 (button).
- `esp_lcd_touch_cst9220` 0.1.2: configures the INT pin as an input. Its reset-pin code runs only when a
  reset pin is given (we pass none), so it drives no outputs.
- `esp_lcd_touch`: installs a GPIO interrupt handler only when a callback is registered; we register
  none and poll instead.

## On the board (2026-11-21)

Touch works; swipes feel smooth. The CST9220 answered with its HYN212 protocol. Pin check at boot: 56/3
open-drain with pull-ups on the I2C signals, 6 input with no output, 7 input with pull-up; motor,
amp and rail unchanged (0, 0, 0). The 80 MHz picture is clean.

| What | 40 MHz (03a) | 80 MHz (03b) |
|---|---|---|
| Ring | 62 fps, flush 2–3 ms | 62 fps, flush 3–4 ms |
| Full-screen frame during a swipe or entrance | render 16–29 ms, flush 7–13 ms | render 15–29 ms, flush 3–6 ms |

The link is no longer the bottleneck: drawing the fade (blending a whole plate at partial opacity) is.
During a swipe LVGL draws about 20–50 frames a second. Next, if needed: slide without the fade, or
render the outgoing plate once into a cached layer.
