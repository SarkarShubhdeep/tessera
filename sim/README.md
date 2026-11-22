# Simulator

The Mosaico's 480×480 screen in a window on the Mac: SDL stands in for the AMOLED, the mouse for touch.

- `../ui/` is the real UI code (orb, plates, tokens, fonts). It uses only LVGL, so the same files compile
  for the board.
- `plates/` holds example `show_plate` JSON, the same input the agent will send.
- `main.c` is the Mac-only part.
- `lv_conf.h`: LVGL 9.5 (pinned in `../third_party/lvgl`), 16-bit colour like the panel, 16 ms refresh
  (60 fps), FPS/CPU counter on.

```bash
cmake -S . -B build -G Ninja && ninja -C build && ./build/tessera_sim
```

The deck loads every plate in `plates/`, or the files you pass:

```bash
./build/tessera_sim plates/01-timer.json my-plate.json
```

It starts on the orb. Swipe left (drag, or the arrow keys) to move through the plates; click the orb to
cycle its states.

`SNAP_DIR=<folder> ./build/tessera_sim` saves one raw RGB565 frame per orb state and plate, runs a
scripted swipe back through the deck, and exits. Convert a frame with:

```bash
ffmpeg -f rawvideo -pixel_format rgb565le -video_size 480x480 -i 01-timer.rgb565 01-timer.png
```

The gallery in `../design/gallery/` is made this way.

The FPS shown is the Mac's, not the board's. On the board the display link is the limit, so keep
animations to small regions and backgrounds black.
