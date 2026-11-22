# Pin review 03: the microphone

**Status:** approved 2026-11-22, flashed (app slot only) and running (results at the end).
**Build:** step 03b plus the microphone. While the ring is listening, its spikes follow the real
microphone level. Speaking still uses the stand-in (nothing plays sound yet). Still no speaker, motor,
Wi-Fi or modules.

Everything in pin reviews [01](pin-review-01-screen.md) and [02](pin-review-02-input.md) stays as it
is. This review lists only what changes.

## New pins

| # | When | GPIO | What | Set to | Why it's safe |
|---|---|---|---|---|---|
| 14 | Mic start | **54** | I2S master clock (MCLK) to the audio chip | Output, 4.096 MHz clock | A clock into the ES8311's MCLK input; nothing else is on this line |
| 15 | Mic start | **37** | I2S bit clock (BCLK) | Output, 512 kHz clock | Clock into the ES8311 only |
| 16 | Mic start | **49** | I2S word select (WS) | Output, 16 kHz | Clock into the ES8311 only |
| 17 | Mic start | **40** | I2S data in (DIN) | **Input** | Microphone samples from the ES8311 |

The three clocks are new outputs, but each goes only to the audio chip, as on the factory firmware.

**Deliberately not touched:**

| GPIO | What | Why |
|---|---|---|
| **45** | Speaker amplifier enable | Stays **low** (amp off), as set at boot. The codec driver is given no amp pin (`pa_pin = -1`, no GPIO interface), and its amp code returns early in that case |
| **52** | I2S data out (to the speaker path) | Not configured: the I2S channel is receive-only |

On the v1.2 board the audio chip has no separate power switch (Espressif's board file marks the old
codec power pin unused; that pin is now the I2C data line), so no power pin changes.

## What goes over I2C

The touch chip as before, plus the ES8311 (`0x19`): its ADC (the microphone path) is set up and its
input gain set. Its DAC (the speaker path) is left off.

## Checks

**Before (no board needed):** build with no warnings; list every pin-touching call in our code and the
new components (`esp_codec_dev` and the I2S driver) and match it to this table.

**On the board:** app slot only, as before. The boot log prints the pin check (now with 37, 40, 45, 49,
52 and 54) and, every second, the loudest microphone level it heard. Stop rule unchanged: any buzz, any
sound from the speaker, or a mismatched pin means hold POWER and restore.

## Build audit (2026-11-22)

- Builds with no warnings. App 977 KB (`0xf4230`), 46% of the slot free. Bootloader unchanged. The
  simulator still builds.
- `main/mic.c` names only the four I2S pins (via `board.h`); `dout` is `I2S_GPIO_UNUSED`.
- `esp_codec_dev` 1.6.2: its GPIO helper (`audio_codec_gpio.c`) is not linked into the build at all
  (checked in the link map). The ES8311 driver touches the amplifier pin only when given both a pin and
  a GPIO interface; it gets neither.

## On the board (2026-11-22)

The spikes follow your voice. No buzz, no sound from the speaker. Pin check: MCLK 54, BCLK 37 and WS 49
carry the I2S signals, DIN 40 is an input, GPIO45 is still 0 (amp off), and GPIO52 is untouched.
One harmless error line at start: `i2s_channel_disable ... not been enabled yet`, from the codec library
stopping the channel before its first start.

Loudest level per second over 43 seconds: quiet room −49 to −51 dBFS, speech mostly −36 to −19,
one peak at −0.4 (very close to the mic). The level mapping was −60…−15 dBFS, so silence still moved
the spikes a little; it is now −47…−20 (in the next build).
