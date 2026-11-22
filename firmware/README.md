# Firmware

The ESP-IDF project that runs the surface (`../ui/`, the same code as the Mac simulator) on the
ESP-Mosaico v1.2.

**Now: step 03a, screen only.** Display up, ring and plates, a fixed demo that logs the frame rate.
No touch, audio, motor, Wi-Fi or modules. Every pin it drives, and the flash plan, are in
[../docs/pin-review-01-screen.md](../docs/pin-review-01-screen.md).

```
. ~/esp/esp-idf/export.sh
idf.py --preview set-target esp32s31   # once
idf.py --preview build
```

Needs ESP-IDF 6.1 (the S31 is a preview target). Don't run `idf.py flash`: see the pin review for the
one-slot flash command.

| Path | What |
|---|---|
| `main/board.c` | The only file that drives GPIOs: motor and amp held off, 3.3 V rail ramp, CO5300 over QSPI |
| `main/main.c` | LVGL display (two 48-line DMA buffers, even-aligned areas for the CO5300), demo loop |
| `components/tessera_ui` | Builds `../ui/` and cJSON as a component |
| `partitions.csv` | The factory table, unchanged |
| `boards/esp_mosaico/usb_hs_console` | TinyUSB console on the Type-C port, with esptool auto-download |
| `boards/esp_mosaico/*.yaml`, `setup_device.c` | The earlier Board Manager 0.6 definition for ESP-Claw. Not used by this build |
