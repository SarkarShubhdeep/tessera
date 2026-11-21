# Decisions

A log of the choices that shape Tessera, newest last. Each says what we chose, what we didn't, and why.

## 001: C + LVGL, not Rust (2026-11-21)

**Chose:** C and LVGL 9.5 on ESP-IDF.
**Considered:** Rust (esp-hal or esp-idf-sys), Slint.

Checked in November 2026:

- **esp-hal** (bare-metal Rust) gained ESP32-S31 support in v1.2.0, but has no LCD driver and no
  confirmed I2S (audio) driver for it.
  ([changelog](https://github.com/esp-rs/esp-hal/blob/main/esp-hal/CHANGELOG.md))
- **esp-idf-sys** (Rust on top of ESP-IDF) stops at the ESP32-P4 and has no ESP-IDF 6.1 support, so no
  S31. ([repo](https://github.com/esp-rs/esp-idf-sys))
- **Slint** runs on the ESP32-S3 through ESP-IDF, but not on the S31, and its free license for
  embedded use is GPLv3. ([license](https://github.com/slint-ui/slint/blob/master/LICENSE.md))

C + LVGL is the only stack that runs on both the S31 and S3 boards today. The engine/surface boundary
(see [architecture](architecture.md)) leaves room for a Rust component once esp-idf-sys supports the S31.

## 002: A new surface on ESP-Claw's engine, not a fork (2026-11-21)

**Chose:** keep ESP-Claw's agent core; write the whole UI ourselves.

ESP-Claw is about 133,000 lines that do everything: agent, tools, chat apps, Lua apps, UI. Its agent
core is good. Its UI is a set of Lua scripts drawn at a 30 fps cap. Tessera reuses the engine and
replaces the surface.

## 003: Claude only, keep MCP (2026-11-21)

**Chose:** a lighter engine that talks to the Claude API directly. Telegram, QQ, Feishu and WeChat are
dropped. MCP (client and server) stays, to explore once the device is live.

## 004: An abstract orb, not a character (2026-11-21)

**Chose:** the device's presence is an orb of light. A face reads as a desk pet; Tessera aims to be a
calm, general-purpose interface.

## 005: The agent designs plates; the device renders them (2026-11-21)

**Chose:** the agent answers by choosing a plate type from a fixed design system and filling in its
fields. The device renders plates natively.
**Not:** the agent writing code (Lua) that draws pixels, as ESP-Claw does.

Why: native rendering is fast (60 fps), always consistent with the design system, and safe. The agent
can't run arbitrary code to draw. Pinning a plate turns it into an "app" the agent keeps fresh.

## 006: "Plates", flat and sharp (2026-11-21)

**Chose:** the UI unit is a **plate**: flat, sharp corners, true black, one accent.
**Not:** "cards" with rounded corners (Material-expressive style). Cards feel dated and every UI has
them. Plates draw on print and Swiss poster design.

## 007: Simulator first; hardware changes only after a pin review (2026-11-21)

The first ESP-Claw build on the Mosaico ran the vibration motor from boot and the board reset itself
in a loop (see [hardware notes](hardware.md#lesson-the-motor-at-boot)). From now on: UI work happens in
the simulator, and before any flash we list every pin the firmware drives at boot and its level.
