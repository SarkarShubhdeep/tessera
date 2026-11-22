# Review 10: the speaker (Tessera says its answers)

**Status:** draft 2026-11-22, waiting for approval. Nothing built yet.
**Build:** step 09 plus speech out. The board shows the plate, then says a short spoken version of it
through its own speaker, using Groq's text-to-speech. Issue
[#24](https://github.com/SarkarShubhdeep/tessera/issues/24).

This is a **pin review**: it turns on two outputs that every build so far has kept off on purpose.

## Pins

Everything in reviews [01](pin-review-01-screen.md), [02](pin-review-02-input.md) and
[03](pin-review-03-mic.md) stays as it is. What changes:

| # | When | GPIO | What | Set to | Why it's safe |
|---|---|---|---|---|---|
| 18 | Audio start | **52** | I2S data out (DOUT) to the ES8311's DAC input | Output, the I2S data stream (zeros when silent) | It goes only to the codec, as on the factory firmware. Until now the I2S channel was receive-only and 52 was untouched |
| 19 | Only while a reply plays | **45** | NS4150B amplifier enable | **High while playing**, low otherwise | Low at boot (held low from the first instruction, as now) and low again after each reply. High only after the codec's output is running and at the set volume, and low before it stops, so the speaker doesn't pop |

**At boot nothing changes:** GPIO45 is still driven low by the board code before anything else runs, and
GPIO52 is first configured when the audio starts, after the screen and touch, as the microphone pins are
now. The motor (GPIO8) is untouched.

**The amplifier can't be left on:** a 10 s watchdog turns GPIO45 low if playback hasn't finished by then
(the longest reply is about 15 s of speech at most; the watchdog is restarted with each chunk of audio).
Any error in playback also turns it low.

## The audio path

- **I2S0 becomes full duplex:** one transmit and one receive channel on the same clocks (MCLK 54, BCLK 37,
  WS 49), DOUT 52, DIN 40, at **16 kHz, 16-bit**, as now. The microphone keeps working while the speaker is
  idle.
- **The ES8311 runs both halves:** ADC (microphone, as now) and DAC (speaker). Through `esp_codec_dev`,
  as for the microphone, with the codec driver still given **no amplifier pin and no GPIO access**. The
  board code switches GPIO45 itself, so there's one place that decides when the amplifier is on.
- **Volume:** starts at **60%** of the codec's output range. A `key volume <0–100>` setting and a
  field on the setup pages can change it, and 0 means silent. 3 W into a small speaker is loud; start low
  and turn it up.
- **No sound while listening:** the speaker is stopped while the AI button is held, so the microphone
  doesn't hear the board's own voice. **Pressing the AI button while it's speaking stops the voice at once**
  and starts listening.

## The voice: Groq text-to-speech

- `POST https://api.groq.com/openai/v1/audio/speech`, model `canopylabs/orpheus-v1-english`, voice
  `hannah` (one of six: autumn, diana, hannah, austin, daniel, troy), `response_format: "wav"`.
- **The same Groq key** as speech-to-text, the same pinned Google Trust Services roots (`api.groq.com`).
- **At most 200 characters** per request (the model's limit). Longer text is split at sentence ends and
  sent as consecutive requests, the next fetched while the current one plays.
- **The WAV's sample rate** isn't stated in Groq's docs (Orpheus is usually 24 kHz). The board reads it
  from the WAV header and **resamples to 16 kHz** if needed (linear interpolation with a light low-pass
  filter: fine for speech), so the microphone and speaker can share one I2S clock.
- **Cost:** $22 per million characters, so a 100-character reply is about $0.002. Groq's free-tier limits
  for this model are on its console; a rejected request (HTTP 429) just means the plate isn't spoken.

## What gets said

- `show_plate` gets one more field, `speech`: "one or two short sentences to say aloud, at most 200
  characters, plain words, no symbols or units Claude wouldn't say aloud". Claude writes the spoken version
  itself ("It's 54 degrees and cloudy, rain after three"), which reads better than the plate's fields.
- No `speech` field means a silent plate, as today.
- Error plates aren't spoken.

## Timing

The plate appears as soon as Claude answers, as now. The speech request starts at the same moment, and
the voice follows. The log adds each step, from letting go of the button:

`speech: transcribe N ms · claude N ms · tts N ms (first audio at N ms) · played N ms`

That's the "how fast can Groq and Claude be" measurement.

## The ring

`speaking` now follows the real output level (the same 0–255 scale as the microphone), replacing the
stand-in curve. The ring speaks only while audio is actually playing.

## What leaves the board

| What | Goes to | New? |
|---|---|---|
| The spoken sentence (Claude's words) | Groq | **New** |
| Recordings, questions, keys, time and place, weather | As before | No |

## Code changes

| File | Change |
|---|---|
| `main/audio.c` (was `mic.c`) | Full-duplex I2S, ES8311 ADC + DAC, playback queue, resampler, output level, GPIO45 with its watchdog |
| `main/tts.c` (new) | Groq speech request, WAV header parsing, sentence splitting |
| `main/claude.c` | `speech` in the `show_plate` schema and prompt; hands the sentence to `tts` with the plate |
| `main/main.c` | AI button stops playback; the speaking ring; `key volume` |
| `main/board.c` | GPIO45 helper (on/off) next to its boot-time hold |
| `main/keys.c` | `volume` setting (0–100) |
| setup pages | Volume field |

## Checks

**Before (no board needed):**
- Builds with no warnings. The pin dump lists GPIO52 as the I2S output and GPIO45 low.
- Every write to GPIO45 is in `board.c`. The codec driver gets no GPIO interface (checked in the link map,
  as in review 03).
- The resampler and WAV parser checked on the Mac with a synthetic 24 kHz WAV.
- No log line prints the spoken text or the key.

**On the board:** app slot only, backup ready. **Start with the volume at 30%** and the board on the desk,
not next to your ear.
1. Boot: no sound, no pop, GPIO45 low in the pin dump.
2. Ask something: the plate, then the voice. Timings in the log. GPIO45 low again after.
3. Press the AI button while it speaks: the voice stops at once and listening starts.
4. Turn the volume to 0: silent plate.

**Stop rule, changed:** sound from the speaker is now expected **only after a question**. Stop and hold
POWER if there's any buzz from the motor, any sound or pop at boot or while idle, a sound that doesn't stop,
or a mismatched pin.
