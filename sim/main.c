/* Mac simulator for the Mosaico UI: a 480x480 SDL window standing in for the AMOLED, the mouse
 * standing in for touch. Only this file is simulator-specific; ui/ is shared with the board. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "lvgl.h"
#include "ui/ui.h"

#define SCREEN_W 480
#define SCREEN_H 480

static uint32_t sdl_ticks(void)
{
    return SDL_GetTicks();
}

int main(void)
{
    lv_init();
    lv_tick_set_cb(sdl_ticks);

    lv_display_t *disp = lv_sdl_window_create(SCREEN_W, SCREEN_H);
    lv_sdl_window_set_title(disp, "Tessera (simulator)");
    lv_sdl_mouse_create();

    ui_create();

    /* SNAP_DIR=<dir>: save one raw RGB565 frame per agent state, then exit (used to review the UI). */
    const char *snap_dir = getenv("SNAP_DIR");
    if (snap_dir) {
        for (int st = 0; st < UI_STATE_COUNT; st++) {
            ui_set_state((ui_state_t)st);
            uint32_t until = SDL_GetTicks() + 700;
            while (SDL_GetTicks() < until) {
                lv_timer_handler();
                SDL_Delay(5);
            }
            /* DIRECT render mode keeps the whole frame in the display's buffer (RGB565). */
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(disp);
            lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
            char path[512];
            snprintf(path, sizeof(path), "%s/state%d.rgb565", snap_dir, st);
            FILE *f = buf ? fopen(path, "wb") : NULL;
            if (!f) {
                fprintf(stderr, "snapshot failed for state %d\n", st);
                return 1;
            }
            for (uint32_t y = 0; y < SCREEN_H; y++) {
                fwrite(buf->data + y * buf->header.stride, 2, SCREEN_W, f);
            }
            fclose(f);
        }
        return 0;
    }

    /* With LV_SDL_DIRECT_EXIT the process exits when the window closes. */
    while (1) {
        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms > 5) {
            wait_ms = 5;
        }
        SDL_Delay(wait_ms);
    }
    return 0;
}
