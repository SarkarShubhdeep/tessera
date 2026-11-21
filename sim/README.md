# Simulator

The Mosaico's 480×480 screen in a window on the Mac: SDL stands in for the AMOLED, the mouse for touch.

- `ui/` is the real UI code and uses only LVGL, so the same files compile for the board.
- `main.c` is the Mac-only part.
- `lv_conf.h`: LVGL 9.5 (pinned in `../third_party/lvgl`), 16-bit colour like the panel, 16 ms refresh
  (60 fps), FPS/CPU counter on.

```bash
cmake -S . -B build -G Ninja && ninja -C build && ./build/tessera_sim
```

`SNAP_DIR=<folder> ./build/tessera_sim` saves one raw RGB565 frame per orb state and exits:

```bash
ffmpeg -f rawvideo -pixel_format rgb565le -video_size 480x480 -i state0.rgb565 state0.png
```

The FPS shown is the Mac's, not the board's. On the board the display link is the limit, so keep
animations to small regions and backgrounds black.
