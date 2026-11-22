/* Tessera on the ESP-Mosaico, step 03a: screen only.
 *
 * Brings up the display, runs the same surface as the Mac simulator (ui/), and plays a fixed demo
 * so the frame rate can be measured with nothing touched: the ring goes through its four states,
 * then five plates arrive and are swiped back through. LVGL's monitor logs FPS and CPU every second.
 *
 * No touch, audio, motor, Wi-Fi or modules in this build: see docs/pin-review-01-screen.md. */
#include <math.h>
#include <stdio.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui.h"

static const char *TAG = "tessera";

/* ---------- display: LVGL partial rendering into two DMA buffers ---------- */

#define BUF_LINES 48

static esp_lcd_panel_handle_t s_panel;

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    (void)io;
    (void)e;
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}

static void flush(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
{
    (void)disp;
    /* The panel takes RGB565 big-endian. */
    lv_draw_sw_rgb565_swap(px, lv_area_get_size(a));
    esp_lcd_panel_draw_bitmap(s_panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
}

/* The CO5300 needs windows that start on an even pixel and are an even number of pixels wide/tall. */
static void round_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static lv_display_t *display_start(void)
{
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *disp = lv_display_create(BOARD_LCD_W, BOARD_LCD_H);
    ESP_ERROR_CHECK(board_display_init(on_trans_done, disp, &s_panel));

    size_t bytes = BOARD_LCD_W * BUF_LINES * 2;
    void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(b1 && b2);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);
    lv_display_add_event_cb(disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    return disp;
}

/* ---------- demo ---------- */

extern const char p_timer[] asm("_binary_01_timer_json_start");
extern const char p_number[] asm("_binary_02_number_json_start");
extern const char p_list[] asm("_binary_04_list_json_start");
extern const char p_choice[] asm("_binary_05_choice_json_start");
extern const char p_gauge[] asm("_binary_06_gauge_json_start");
static const char *const PLATES[] = {p_timer, p_number, p_list, p_choice, p_gauge};
#define N_PLATES (sizeof(PLATES) / sizeof(PLATES[0]))

/* Stand-in audio level (the simulator's and landing page's curve) until the codec is wired up. */
static void fake_voice(lv_timer_t *t)
{
    (void)t;
    float s = lv_tick_get() / 1000.0f;
    float syl = fmaxf(0.0f, sinf(s * 6.1f)) * (0.55f + 0.45f * sinf(s * 1.3f));
    float grit = 0.5f + 0.5f * sinf(s * 17.0f);
    float phrase = sinf(s * 0.7f) > -0.85f ? 1.0f : 0.2f;
    ui_set_level((uint8_t)(fminf(1.0f, (syl * 0.85f + grit * 0.15f * syl) * phrase) * 255.0f));
}

/* One step per second; each line of the log says what is on screen, next to the FPS lines. */
static void demo_step(lv_timer_t *t)
{
    (void)t;
    static int step;
    static const ui_state_t RING[] = {UI_STATE_IDLE, UI_STATE_IDLE, UI_STATE_IDLE, UI_STATE_LISTENING,
                                      UI_STATE_LISTENING, UI_STATE_LISTENING, UI_STATE_THINKING,
                                      UI_STATE_THINKING, UI_STATE_SPEAKING, UI_STATE_SPEAKING,
                                      UI_STATE_SPEAKING};
    static const char *const NAMES[] = {"idle", "listening", "thinking", "speaking"};
    const int n_ring = sizeof(RING) / sizeof(RING[0]);

    if (step < n_ring) {
        if (step == 0 || RING[step] != RING[step - 1]) {
            ESP_LOGI(TAG, "demo: ring %s", NAMES[RING[step]]);
            ui_set_state(RING[step]);
        }
    } else if (step < n_ring + (int)N_PLATES * 2) {   /* a new plate every 2 s */
        int i = step - n_ring;
        if (i % 2 == 0) {
            cJSON *spec = cJSON_Parse(PLATES[i / 2]);
            ESP_LOGI(TAG, "demo: plate %d enters", i / 2 + 1);
            ui_show_plate(spec, true);
            cJSON_Delete(spec);
        }
    } else if (ui_deck_pos() >= 0) {                   /* swipe back to the ring, one a second */
        ESP_LOGI(TAG, "demo: swipe to %d", ui_deck_pos() - 1);
        ui_swipe(-1);
        return;
    } else {
        step = 0;
        ESP_LOGI(TAG, "demo: free heap %u internal, %u PSRAM",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return;
    }
    step++;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Tessera screen-only build");
    board_dump_pins();   /* motor and amp, already held low by the constructor in board.c */
    lv_display_t *disp = display_start();
    board_dump_pins();   /* again, with the rail and the panel up */

    ui_create();
    lv_timer_create(fake_voice, 33, NULL);
    lv_timer_create(demo_step, 1000, NULL);
    (void)disp;

    while (1) {
        uint32_t wait = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(wait < 1 ? 1 : (wait > 10 ? 10 : wait)));
    }
}
