# Architecture

Draft, 2026-11-21. This will change as we build.

```
 ┌──────────────── Surface (Tessera) ────────────────┐
 │  orb · plates · gestures · haptics · sounds         │   C + LVGL 9.5
 │  design system (tokens, plate types, motion)        │   same code in sim/ and firmware
 └───────────────▲─────────────────────┬──────────────┘
          events │                     │ intents
 ┌───────────────┴─────────────────────▼──────────────┐
 │  Engine (slim ESP-Claw core)                        │
 │  agent loop · Claude API · tools · MCP · memory     │
 └───────────────▲─────────────────────────────────────┘
                 │
 ┌───────────────┴─────────────────────────────────────┐
 │  Board (Espressif Board Manager)                     │
 │  display · touch · audio · button · motor · IMU      │
 └──────────────────────────────────────────────────────┘
```

## The contract between engine and surface

The two halves talk only through a small set of messages, so the surface can run in the simulator
against a fake engine.

**Engine → surface (events)**
- `state`: idle · listening · thinking · speaking · error
- `plate`: show a plate (type plus fields, see [design system](design-system.md))
- `plate_update`: new values for a plate already shown or pinned
- `level`: microphone or speaker level, for the orb

**Surface → engine (intents)**
- `talk_start` / `talk_end`: the AI button is held, then released
- `act`: the AI button is tapped while a plate is shown ("about this")
- `choose`: a plate's action was tapped
- `pin` / `unpin`

## Plates are data, not code

The agent gets one tool, `show_plate`, whose input schema *is* the design system: a plate type
and its fields. The engine validates the input; the surface renders it. A plate the surface doesn't
understand falls back to a plain text plate, so nothing breaks.

## Scaling to other boards

Each board declares a **size class** (S ≈ 240 px, M ≈ 360–412 px, L ≈ 466–480 px), **shape**
(square or round) and **capabilities** (mic, speaker, motor, IMU, camera, buttons). The design tokens
scale with the size class; features without hardware degrade gracefully (no mic: tap-only; no motor:
visual detents).

## Performance budget (Mosaico)

- The display link is 4-line QSPI. One full 480×480 RGB565 frame is about 11.5 ms at 80 MHz, so
  about 85 fps is the ceiling before any drawing.
- Animations stay inside small regions (60 fps); full-screen plate transitions aim for 45–60 fps.
- LVGL refresh period 16 ms (ESP-Claw's default is 33 ms, a 30 fps cap); tear-free updates via the
  panel's TE signal (GPIO43).
- The S31 has no GPU. Its pixel accelerator (PPA: rotate, scale, blend) and hardware JPEG help with
  images and the camera, not much with frame rate.
