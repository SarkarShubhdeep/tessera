#!/bin/sh
# Regenerates the LVGL bitmap fonts from ../../fonts/*.ttf. Run from this directory.
set -eu
TTF=../../fonts
TEXT="0x20-0x7E,0xB0,0xB7,0xD7,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2026,0x2190,0x2192"
conv() { npx -y lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --format lvgl --lv-include lvgl.h \
  --font "$TTF/$1" -r "$2" --size "$3" --lv-font-name "$4" -o "$4.c"; }
conv GeistPixel-Regular.ttf "0x20,0x25,0x2B,0x2D,0x2E,0x30-0x3A,0xB0" 140 tsr_pixel_140   # numerals only
conv GeistPixel-Regular.ttf "$TEXT" 32 tsr_pixel_32
conv GeistMono-Medium.ttf   "$TEXT" 15 tsr_mono_15
conv GeistMono-Medium.ttf   "$TEXT" 17 tsr_mono_17
conv GeistMono-Medium.ttf   "$TEXT" 24 tsr_mono_24
conv GeistMono-SemiBold.ttf "$TEXT" 28 tsr_mono_28
conv GeistMono-SemiBold.ttf "$TEXT" 44 tsr_mono_44
