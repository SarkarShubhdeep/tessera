# Firmware

Not buildable yet. This will hold the ESP-IDF project that combines the surface (`../sim/ui/`), the
slimmed ESP-Claw engine (`../third_party/esp-claw`) and the board definitions.

## boards/esp_mosaico

A Board Manager 0.6 definition of the ESP-Mosaico v1.1/v1.2 (the version ESP-Claw pins), flattened from
Espressif's official 0.7-format board. Tested so far: boots, display and touch controllers initialise,
codec answers on I2C. The motor fix (motor not created as a `gpio_ctrl` device) has **not** been run
on the board yet. See [../docs/hardware.md](../docs/hardware.md).
