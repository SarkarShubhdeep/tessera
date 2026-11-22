/* Tessera design tokens: the single source for colour, type, spacing and motion on the device.
 * They mirror the landing page (site/index.html), so the website and the firmware can't drift apart.
 *
 * Geometry is authored on a 480 x 480 canvas and scaled with TS_PX(), so layouts follow the display
 * size. Bitmap fonts don't scale; each size class gets its own font set (only L exists so far). */
#pragma once

#include "lvgl.h"

/* ---------- colour ---------- */
#define TS_BLACK        lv_color_hex(0x000000)  /* AMOLED off: the background, always */
#define TS_INK          lv_color_hex(0xF2EEE8)  /* primary text */
#define TS_DIM          lv_color_hex(0x6B655E)  /* labels, secondary text */
#define TS_RULE         lv_color_hex(0x2A2622)  /* 2 px rules, empty segments, outlines */
#define TS_ACCENT       lv_color_hex(0xFF5A1F)  /* the AI button's orange: live or actionable */
#define TS_ON_ACCENT    lv_color_hex(0x0A0908)  /* text on an accent fill */

/* ---------- geometry (480-canvas units) ---------- */
#define TS_CANVAS       480
int32_t ts_px(int32_t canvas_units);           /* scales a 480-canvas length to this display */
#define TS_PX(v)        ts_px(v)

#define TS_MARGIN       36   /* plate edge padding */
#define TS_RULE_W       2    /* rule thickness */
#define TS_GAP_S        12
#define TS_GAP_M        16
#define TS_GAP_L        26
#define TS_ROW_PAD      18   /* list row vertical padding */

/* ---------- type (size class L: 466-480 px) ---------- */
LV_FONT_DECLARE(tsr_pixel_140);  /* Geist Pixel, numerals: the hero value of a plate */
LV_FONT_DECLARE(tsr_pixel_32);   /* Geist Pixel: display text */
LV_FONT_DECLARE(tsr_mono_15);    /* Geist Mono Medium: footers */
LV_FONT_DECLARE(tsr_mono_17);    /* Geist Mono Medium: plate headers */
LV_FONT_DECLARE(tsr_mono_24);    /* Geist Mono Medium: secondary text, clock, status */
LV_FONT_DECLARE(tsr_mono_28);    /* Geist Mono SemiBold: list rows, options */
LV_FONT_DECLARE(tsr_mono_44);    /* Geist Mono SemiBold: questions, answers, units */

#define TS_FONT_HERO    (&tsr_pixel_140)
#define TS_FONT_DISPLAY (&tsr_pixel_32)
#define TS_FONT_FOOT    (&tsr_mono_15)
#define TS_FONT_HEAD    (&tsr_mono_17)
#define TS_FONT_SUB     (&tsr_mono_24)
#define TS_FONT_ROW     (&tsr_mono_28)
#define TS_FONT_LEAD    (&tsr_mono_44)

#define TS_TRACK_LABEL  2    /* letter spacing for uppercase labels, px */

/* ---------- motion ----------
 * Budget: on the Mosaico a full-screen redraw costs ~11.5 ms of display link alone, so full-screen
 * moves are short and rare; continuous animation stays inside small regions. */
#define TS_MS_ENTER     260  /* plate in: ease-out */
#define TS_MS_EXIT      130  /* plate out: ease-in, half the entrance */
#define TS_MS_SWIPE     220  /* plate to plate */
#define TS_ENTER_RISE   18   /* canvas units a plate rises while fading in */
#define TS_SWIPE_SHIFT  60   /* canvas units a plate slides on a swipe */
