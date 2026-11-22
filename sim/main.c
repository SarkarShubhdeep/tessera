/* Tessera simulator: a 480 x 480 SDL window standing in for the AMOLED, the mouse for touch, the keyboard
 * for the AI button. Only this file is simulator-specific; ../ui/ is shared with the board.
 *
 *   ./tessera_sim [plate.json ...]      load plates into the deck (default: every .json in plates/)
 *   left/right arrows or drag           swipe through the deck (the orb is left of the first plate)
 *   click the orb                       cycle its states
 *   SNAP_DIR=dir ./tessera_sim ...      save every plate and orb state as raw RGB565, then exit
 */
#include <SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "lvgl.h"
#include "ui/plates.h"
#include "ui/ui.h"

#define SCREEN_W 480
#define SCREEN_H 480
#define MAX_FILES 32

static uint32_t sdl_ticks(void) { return SDL_GetTicks(); }

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        buf[n] = '\0';
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int default_plates(char **out, int max)
{
    DIR *d = opendir("plates");
    int n = 0;
    if (!d) {
        return 0;
    }
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        size_t len = strlen(e->d_name);
        if (len > 5 && strcmp(e->d_name + len - 5, ".json") == 0) {
            out[n] = malloc(len + 8);
            snprintf(out[n], len + 8, "plates/%s", e->d_name);
            n++;
        }
    }
    closedir(d);
    qsort(out, (size_t)n, sizeof(out[0]), cmp_str);
    return n;
}

static void pump(uint32_t ms)
{
    uint32_t until = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < until) {
        lv_timer_handler();
        SDL_Delay(5);
    }
}

/* DIRECT render mode keeps the whole frame in the display buffer (RGB565). */
static int snap(lv_display_t *disp, const char *dir, const char *name)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.rgb565", dir, name);
    FILE *f = buf ? fopen(path, "wb") : NULL;
    if (!f) {
        fprintf(stderr, "snapshot failed: %s\n", path);
        return 1;
    }
    for (uint32_t y = 0; y < SCREEN_H; y++) {
        fwrite(buf->data + y * buf->header.stride, 2, SCREEN_W, f);
    }
    fclose(f);
    printf("%s\n", path);
    return 0;
}

static const char *base_name(const char *path)
{
    static char name[128];
    const char *s = strrchr(path, '/');
    snprintf(name, sizeof(name), "%s", s ? s + 1 : path);
    char *dot = strrchr(name, '.');
    if (dot) {
        *dot = '\0';
    }
    return name;
}

int main(int argc, char **argv)
{
    lv_init();
    lv_tick_set_cb(sdl_ticks);

    lv_display_t *disp = lv_sdl_window_create(SCREEN_W, SCREEN_H);
    lv_sdl_window_set_title(disp, "Tessera (simulator)");
    lv_sdl_mouse_create();
    lv_indev_t *kb = lv_sdl_keyboard_create();

    ui_create();
    lv_group_t *g = lv_group_create();
    lv_group_add_obj(g, lv_screen_active());
    lv_indev_set_group(kb, g);

    char *files[MAX_FILES];
    int nfiles = 0;
    for (int i = 1; i < argc && nfiles < MAX_FILES; i++) {
        files[nfiles++] = strdup(argv[i]);
    }
    if (nfiles == 0) {
        nfiles = default_plates(files, MAX_FILES);
    }

    const char *snap_dir = getenv("SNAP_DIR");
    if (snap_dir) {
        lv_sysmon_hide_performance(disp);
        static const char *const STATES[] = {"orb-idle", "orb-listening", "orb-thinking", "orb-speaking"};
        for (int st = 0; st < UI_STATE_COUNT; st++) {
            ui_set_state((ui_state_t)st);
            pump(700);
            if (snap(disp, snap_dir, STATES[st])) {
                return 1;
            }
        }
        for (int i = 0; i < nfiles; i++) {
            char *json = read_file(files[i]);
            cJSON *spec = json ? cJSON_Parse(json) : NULL;
            ui_show_plate(spec, false);
            pump(300);
            if (snap(disp, snap_dir, base_name(files[i]))) {
                return 1;
            }
            cJSON_Delete(spec);
            free(json);
        }
        /* Motion check: swipe back through the whole deck to the orb, mid-animation snapshots included. */
        int steps = ui_deck_size();
        for (int i = 0; i < steps; i++) {
            ui_swipe(-1);
            pump(i == 1 ? 90 : 300);
            if (i == 1 && snap(disp, snap_dir, "motion-swipe-mid")) {
                return 1;
            }
        }
        pump(300);
        printf("swiped to position %d of %d\n", ui_deck_pos(), ui_deck_size());
        return ui_deck_pos() == -1 ? 0 : 2;
    }

    /* Load the deck, then start on the orb: swipe left to see the plates. */
    for (int i = 0; i < nfiles; i++) {
        char *json = read_file(files[i]);
        if (!json) {
            fprintf(stderr, "can't read %s\n", files[i]);
            continue;
        }
        cJSON *spec = cJSON_Parse(json);
        ui_show_plate(spec, false);
        cJSON_Delete(spec);
        free(json);
    }
    ui_set_state(UI_STATE_IDLE);

    while (1) { /* LV_SDL_DIRECT_EXIT ends the process when the window closes */
        uint32_t wait_ms = lv_timer_handler();
        SDL_Delay(wait_ms > 5 ? 5 : wait_ms);
    }
    return 0;
}
