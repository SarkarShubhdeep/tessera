# Design system

In progress. The design system is also the agent's vocabulary: the plate types below are exactly
what the agent can put on screen.

## Principles

- **Flat and sharp.** Square corners everywhere. Rules (1–2 px lines) instead of shadows or boxes.
- **True black.** AMOLED pixels are off on black: deep contrast, less power, nothing to redraw.
- **One accent.** Orange (`#FF5A1F`, from the Mosaico's AI button) marks what is live or actionable.
- **Typography does the work.** Large numerals, a clear type scale, generous margins.
- **Motion has a budget.** Small regions animate freely; full-screen moves are short and purposeful.

## Plate types (first draft)

| Plate | Fields | Example |
|---|---|---|
| `answer` | text (short), source? | "Paris." |
| `number` | value, unit, label | "18°", "Living room" |
| `timer` | label, seconds | Tea, 4:00, counting down |
| `list` | title, items (≤ 5) | Today's three meetings |
| `choice` | question, options (2–4) | "Lights: warm or cool?" |
| `gauge` | value, min, max, label | Battery 84% |
| `image` | source, caption | A camera photo |

## Tokens (to define)

- Size classes: S (240), M (360–412), L (466–480)
- Type scale per size class
- Spacing grid
- Colour: black, ink, dim ink, accent, error
- Motion: durations and easing for enter, exit, state change
