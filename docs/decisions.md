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

## 004: An abstract presence, not a character (2026-11-21)

**Chose:** the device's presence is abstract light (now a ring of segments, see 008). A face reads as a
desk pet; Tessera aims to be a calm, general-purpose interface.

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

## 008: The presence is a ring of square segments (2026-11-22)

**Chose:** one ring of 24 square segments that morphs between states.
**Tried and dropped:** a solid circle (too plain, and the only round thing in a sharp system), and a
pixel orb of squares arranged in a circle that changed shape per state (it swapped shapes instead of
morphing, and its motion was canned).

Common agent-UI practice, which the ring follows:
- one object that morphs between states, never a swap
  ([orb-ui](https://github.com/alexanderqchen/orb-ui), [LiveKit](https://docs.livekit.io/frontends/agents-ui/audio-visualizer/prebuilt/))
- listening follows the user's voice and speaking follows the agent's, from real audio levels
- thinking is the one steady, non-audio motion
  ([Stream](https://getstream.io/chat/docs/sdk/flutter/ai-components/voice-visualizations/),
  [Rive guide](https://dev.to/uianimation/building-a-smart-orb-animation-for-ai-assistants-in-rive-3off))
- idle is nearly still; a dashed ring at rest reads as "loading", so idle is solid
- presence shouldn't block content: Siri moved to an edge glow
  ([9to5Mac](https://9to5mac.com/2024/11/03/new-apple-intelligence-siri-looks-different-works-the-same/)).
  Not used yet; a candidate for when a plate is shown while the agent speaks.

**Revised 2026-11-22:** on the board, listening and speaking looked the same (both spiked outward).
Listening now opens the ring out and points its spikes inward (sound coming in); speaking stays tight
and points outward (sound going out). Direction, not just size, tells the two apart.

## 009: Speech-to-text through Groq's hosted Whisper (2026-11-22)

**Chose:** the device sends the clip recorded while the AI button is held to Groq's
`whisper-large-v3-turbo` (an open-source model) and sends the returned text to Claude.
**Considered:** on-device recognition, Whisper on the Mac (whisper.cpp), and other hosted speech APIs.

Why a speech-to-text step at all: the Claude Messages API takes text, images and documents, not audio.
The Claude apps do this step themselves before sending text to the model; the device has to do the same.

- **On the device:** free speech recognition needs a model of tens of millions of parameters (Whisper
  tiny is about 39 M, ~75 MB). The S31 has 16 MB of PSRAM and no accelerator. Espressif's ESP-SR runs
  here but only recognises a wake word and a fixed list of commands, not free speech. Kept as a later
  add-on for instant fixed commands.
- **Whisper on the Mac (whisper.cpp):** free and private (audio stays at home), but the device would only
  work while the Mac is on. The goal is Mosaico plus Claude, nothing else.
- **Hosted APIs with a free tier (checked November 2026):** Groq (Whisper, about 2,000 requests and 8 hours
  of audio a day), Cloudflare Workers AI (Whisper, ~200 minutes a day), Google Cloud (60 minutes a
  month), ElevenLabs (~30 minutes a month), Deepgram and AssemblyAI (one-time sign-up credit).

**Why Groq:** it is built for low latency (a short clip transcribes in well under a second), its free
tier covers far more than a desk device uses, and it runs an open-source model, so the provider can be
swapped (Cloudflare runs the same model) without changing what the device expects. Its endpoint takes
16 kHz WAV, which is what the mic already records, and `api.groq.com` uses the same certificate chain
(Google Trust Services) the firmware already trusts.

**Costs:** the clip leaves the home network for Groq, and a second key is needed, which the owner enters
on the device; it is stored in Tessera's own settings namespace, separate from ESP-Claw's. Free tiers
change, so check Groq's own limits before relying on them.
