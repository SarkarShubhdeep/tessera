# Hardware notes: ESP-Mosaico

Board revision v1.2 (ESP32-S31NRV16: 16 MB flash, 16 MB PSRAM in package, 128 MB SPI NAND).
Pins below are from Espressif's official board definition (`espressif/esp_boards` 0.7.3, `esp_mosaico`)
and the [user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp-mosaico/user_guide.html).

## Pin map (v1.1 / v1.2)

| Function | Pins / address |
|---|---|
| On-board I2C | SDA 56, SCL 3. Touch CST9220 `0x5A`, codec ES8311 `0x19`, IMU BMI270 `0x69`, magnetometers BMM150 `0x11`/`0x12`, fuel gauge BQ27220 `0x55` |
| Module-slot I2C | SDA 0, SCL 1. Module EEPROMs `0x50` (left) / `0x51` (right) |
| Display (CO5300, QSPI on SPI2) | CS 50, D0 36, D1 51, D2 35, D3 9, SCLK 42, RST 44, TE 43 |
| Touch interrupt | 6 |
| Audio (ES8311, I2S) | MCLK 54, BCLK 37, WS 49, DOUT 52, DIN 40, amplifier enable 45 |
| Buttons | AI 7, BOOT 61 (both active-low) |
| Vibration motor | 8 (active-high) |
| Power | 3.3 V rail 60 (active-low, ramp it on); shutdown 57 (open-drain, active-low) |

## Buttons

- **POWER** (front edge, right): a **short tap** turns it on. Long holds turn it off.
- **BOOT** (front edge, left): hold it while powering on for download mode.
- **AI** (right side): free for the firmware.

## Recovery

Firmware can't break the chip's built-in download mode. If firmware misbehaves:

1. Tap POWER to switch on (or hold BOOT while powering on for download mode).
2. Running `esptool` once on the board's USB port switches it to download mode; it re-appears as a
   new port. Only **one** esptool session works per entry, so go straight to the write.
3. `esptool --chip esp32s31 -p <port> --before no-reset --after no-reset write-flash 0 <backup.bin>`

Keep a full flash backup (`read-flash 0 0x1000000`) **out of the repo**: it contains Wi-Fi and API
credentials.

## Lesson: the motor at boot

ESP-Claw pins Board Manager 0.6. Its `gpio_ctrl` device drives the pin to its *active* level when it
initialises, so a `vibration_motor` device switches the motor **on** at boot. On the Mosaico the
motor's current draw then reset the board, which started the motor again: a buzzing reset loop.
Espressif's official board avoids this with Board Manager 0.7.6's `init_level`. In Tessera's board
file the motor is only a plain output peripheral, held low.

**Rule:** before every flash, list each output pin the firmware drives at boot and its level.

## Display speed

ESP-Claw drives the CO5300 at 40 MHz with a 33 ms refresh period (30 fps cap). The S31's SPI can run
QSPI at 80 MHz; whether this panel accepts 80 MHz still needs testing on the board.

## Lesson: the factory bootloader

The Mosaico ships with a custom bootloader (an ESP-IDF 6.1 development build with an "Idea Run" /
"Vibe Mode" boot menu). An app built with the ESP-IDF 6.1 release crashes under it before USB comes up.
Flash the bootloader from the same ESP-IDF as the app. Keep the factory bootloader backed up to go back.
