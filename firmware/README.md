# Firmware

The ESP-IDF project that runs the surface (`../ui/`, the same code as the Mac simulator) on the
ESP-Mosaico v1.2.

**Now: step 05b.** Screen, touch, AI button, microphone, Claude over Wi-Fi, and voice through Groq.
Each step was reviewed before flashing: [01](../docs/pin-review-01-screen.md),
[02](../docs/pin-review-02-input.md), [03](../docs/pin-review-03-mic.md),
[04](../docs/review-04-claude.md), [05](../docs/review-05-voice.md). Tools, keys, flashing and recovery:
[../docs/setup.md](../docs/setup.md).

```
. ~/esp/esp-idf/export.sh
idf.py --preview set-target esp32s31   # once
idf.py --preview build
```

Needs ESP-IDF 6.1 (the S31 is a preview target). Don't run `idf.py flash`: see [setup](../docs/setup.md#flashing-and-recovery) for the
one-slot flash command.

| Path | What |
|---|---|
| `main/board.c` | The only file that drives GPIOs: motor and amp held off, 3.3 V rail ramp, CO5300 over QSPI |
| `main/main.c` | LVGL display (two 48-line DMA buffers, even-aligned areas for the CO5300), AI button, console |
| `main/mic.c` | ES8311 microphone over receive-only I2S: level for the ring, recording for voice |
| `main/claude.c`, `groq.c` | Claude Messages API and Groq speech to text, over HTTPS with pinned roots |
| `main/settings.c`, `keys.c` | ESP-Claw's saved settings (read only) and Tessera's own Groq key |
| `components/tessera_ui` | Builds `../ui/` and cJSON as a component |
| `partitions.csv` | The factory table, unchanged |
| `boards/esp_mosaico/usb_hs_console` | TinyUSB console on the Type-C port, with esptool auto-download |
| `boards/esp_mosaico/*.yaml`, `setup_device.c` | The earlier Board Manager 0.6 definition for ESP-Claw. Not used by this build |
