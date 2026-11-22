# Pin review 01: screen-only build

**Status:** approved 2026-11-21. Built and audited (below). Not flashed yet.
**Build:** the first Tessera firmware on the ESP-Mosaico v1.2. It draws the ring and the plates, and
nothing else: no touch, no audio, no motor, no Wi-Fi, no modules.

This follows decision 007: before any flash, list every pin the firmware drives at boot and its level.
Sources: our pin map ([hardware.md](hardware.md)) and Espressif's official board code
(`espressif/esp_boards` 0.7.3, `esp_mosaico`: `board_peripherals.yaml`, `board_devices.yaml`,
`board_runtime.c`).

## No Board Manager in this build

The motor bug came from Board Manager setting a pin to its "on" level while it initialised a device.
This build doesn't use Board Manager: every pin is set by a line in Tessera's own code, in the order
below, so the list is the whole truth.

## Pins this build drives, in boot order

| # | When | GPIO | What | Set to | Why it's safe |
|---|---|---|---|---|---|
| 0 | ROM, before any app | **58** | UART0 TX (serial log) | UART TX, idles high | Not ours: the chip's ROM and the factory bootloader drive it on every boot, and the factory app does too. Listed so the table is complete |
| 1 | Before `app_main` | **8** | Vibration motor | Output **low**, pull-down on | Motor off. Done first, before anything else can run. Espressif's board does the same (`init_level: 0`) |
| 2 | Before `app_main` | **45** | Speaker amplifier enable | Output **low** | Amp off. No audio in this build, so there's nothing to amplify. (The ESP-Claw build drove it **high**) |
| 3 | Start of `app_main` | (none) | USB console | TinyUSB CDC | Uses the USB HS PHY's own pads, not GPIOs. Keeps logs and the esptool auto-download working, so recovery stays easy |
| 4 | Display power | **60** | 3.3 V rail (display and module slots) | Ramped from high (off) to **low (on)** over 50 ms, then held low | Same code and timing as Espressif's board and the factory firmware. The ramp limits inrush |
| 5 | Display bus | **42** | QSPI clock | SPI2 clock | Driven by the SPI peripheral, 40 MHz to start (ESP-Claw's proven speed) |
| 6 | Display bus | **36, 51, 35, 9** | QSPI data 0–3 | SPI2 data | Display data only |
| 7 | Display bus | **50** | Display chip-select | SPI2 CS, idle high | Display only |
| 8 | Display reset | **44** | Display reset | Low for 10 ms, then **high** | Standard panel reset; the driver does this once |
| 9 | Display init | (none) | CO5300 commands | Sleep out, RGB565, brightness, display on | Commands over the bus, no pins. Same table that already ran on the board in the ESP-Claw build |

That's all. **Ten GPIOs** are touched by Tessera's code: 8, 45, 60, 42, 36, 51, 35, 9, 50, 44, plus
GPIO58, which the ROM already drives before any app runs.

## Pins this build does not touch

They stay as the chip leaves them after reset (inputs).

| GPIO | What | Why it's left alone |
|---|---|---|
| **57** | Power switch path | Driving it low cuts power. Not an app pin; never touched |
| 56, 3 | On-board I2C (touch, codec, IMU, fuel gauge) | Touch comes in the next step |
| 6 | Touch interrupt | Next step |
| 7 | AI button | Next step (input only) |
| 61 | BOOT button | Input; needed for download mode |
| 43 | Display TE (frame sync) | Not used yet |
| 54, 37, 49, 52, 40 | I2S audio | No audio in this build |
| 0, 1 | Module-slot I2C | No modules in this build |
| 33 | Camera D2, shares the USB-JTAG pad | Releasing it would cut the debug pad |
| **34** | Camera module flash LED | Espressif only sets it (to off) when the camera starts. See the first-flash steps |

## Build audit (2026-11-21)

- `idf.py --preview build`: no warnings. App 845 KB (`0xd2c70`), 53% of the slot free.
- Every call that configures a pin in our code and the components we link was listed and matched:
  - `main/board.c` is the only file that names a GPIO: motor 8 and amp 45 (constructor), rail 60
    (LEDC ramp, then plain GPIO), the SPI bus pins, CS 50, RST 44.
  - `esp_lcd_co5300` 2.2.0 configures only the reset pin it is given (44).
  - `esp_tinyusb` has a VBUS-monitor GPIO, used only when `self_powered = true`; the USB console uses
    the default config (`self_powered = false`), so it never runs. The `tinyusb/hw/bsp` files are
    examples and are not compiled.
  - LVGL and the `ui/` sources touch no hardware.
- Partition table: byte-for-byte identical to the factory table (read from the backup).

## Flash plan: one slot only

Tessera's app goes into the factory's `main_app` slot (`ota_0`, `0x210000`). Only that slot is written:
the bootloader, the partition table, `otadata` and the NVS partitions (Wi-Fi and API settings) stay
untouched.

```
esptool --chip esp32s31 -p <port> --before no-reset --after no-reset write-flash 0x210000 build/tessera.bin
```

Restore the factory app into the same slot (the slot image is cut from the full backup, kept outside the repo):

```
esptool --chip esp32s31 -p <port> --before no-reset --after no-reset write-flash 0x210000 backups/mosaico/main_app-0x210000.bin
```

Don't use `idf.py flash`: it would also write the bootloader, partition table and `otadata`.

## Checks before and after the flash

**Before (no board needed):** done; see the build audit.

**On the board:**
1. Remove the camera and interaction modules first. The 3.3 V rail powers the slots, and with no camera code GPIO34 (its flash LED) isn't set. With no modules attached, that question goes away.
2. Backups are ready: the full factory image and the `main_app` slot (`backups/mosaico/`, kept out of the repo).
3. Flash the app slot only (flash plan above).
4. At boot the firmware prints the state of every output pin (`gpio_dump_io_configuration`). We compare it to this table.
5. **Stop rule:** any buzz, any reset loop, or a pin that doesn't match means hold POWER to switch it off, then restore the factory image (steps in [hardware.md](hardware.md#recovery)).

## What this step measures

- Does the 480 × 480 CO5300 come up with our init table, the right way round?
- Ring frame rate at 40 MHz QSPI, then at 80 MHz if the panel accepts it.
- Swipe frame rate: is it above 45 fps, or do swipes fall back to a plain slide?
