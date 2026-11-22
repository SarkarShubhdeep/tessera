/* Tessera on the ESP-Mosaico, step 05a: screen, touch, AI button, mic level, and Claude over Wi-Fi.
 * A line typed into the USB console is sent to Claude; the answer arrives as a plate (review 04).
 *
 * Runs the same surface as the Mac simulator (ui/). Tap the ring to cycle its states, swipe through
 * five example plates, hold the AI button to "talk" (a stand-in until the engine exists). LVGL's
 * monitor logs FPS and CPU to the console. While listening, the ring follows the real microphone.
 *
 * No audio, motor, Wi-Fi or modules: see docs/pin-review-01-screen.md and -02-input.md. */
#include <math.h>
#include <stdio.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mic.h"
#include "claude.h"
#include "net.h"
#include "settings.h"
#include <string.h>
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

/* Audio level for the ring: the real microphone while listening; the stand-in curve (the simulator's
 * and landing page's) while speaking, until the speaker path exists. */
static void ring_level(lv_timer_t *t)
{
    (void)t;
    if (ui_get_state() == UI_STATE_LISTENING) {
        ui_set_level(mic_level());
        return;
    }
    float s = lv_tick_get() / 1000.0f;
    float syl = fmaxf(0.0f, sinf(s * 6.1f)) * (0.55f + 0.45f * sinf(s * 1.3f));
    float grit = 0.5f + 0.5f * sinf(s * 17.0f);
    float phrase = sinf(s * 0.7f) > -0.85f ? 1.0f : 0.2f;
    ui_set_level((uint8_t)(fminf(1.0f, (syl * 0.85f + grit * 0.15f * syl) * phrase) * 255.0f));
}

/* ---------- input ---------- */

static esp_lcd_touch_handle_t s_touch;

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    esp_lcd_touch_point_data_t pt;
    uint8_t n = 0;
    esp_lcd_touch_read_data(s_touch);
    if (esp_lcd_touch_get_data(s_touch, &pt, &n, 1) == ESP_OK && n > 0) {
        data->point.x = pt.x;
        data->point.y = pt.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* AI button stand-in for the engine: hold = listening; release = thinking 1.5 s, speaking 3 s, rest. */
static void ai_button_poll(lv_timer_t *t)
{
    (void)t;
    static bool was_down;
    static uint32_t released_at;
    bool down = board_ai_button_pressed();
    uint32_t now = lv_tick_get();
    if (down && !was_down) {
        ESP_LOGI(TAG, "AI button: listening");
        ui_set_state(UI_STATE_LISTENING);
        released_at = 0;
    } else if (!down && was_down) {
        ESP_LOGI(TAG, "AI button: released, thinking");
        ui_set_state(UI_STATE_THINKING);
        released_at = now ? now : 1;
    } else if (!down && released_at) {
        uint32_t dt = lv_tick_diff(now, released_at);
        if (dt > 4500) {
            ui_set_state(UI_STATE_IDLE);
            released_at = 0;
        } else if (dt > 1500) {
            static uint32_t spoke_for;
            if (spoke_for != released_at) {
                ui_set_state(UI_STATE_SPEAKING);
                spoke_for = released_at;
            }
        }
    }
    was_down = down;
}

/* ---------- Claude: console questions in, plates out ---------- */

/* Reads lines from the USB console. Any line is a question. */
static void console_task(void *arg)
{
    (void)arg;
    char line[256];
    int n = 0;
    while (1) {
        int c = getchar();
        if (c == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            line[n] = '\0';
            if (n > 0) {
                if (!net_is_up()) {
                    ESP_LOGW(TAG, "not connected; question dropped");
                } else if (!claude_ask(line)) {
                    ESP_LOGW(TAG, "busy; question dropped");
                }
            }
            n = 0;
        } else if (n < (int)sizeof(line) - 1) {
            line[n++] = (char)c;
        }
    }
}

/* UI side: think while a request is in flight, draw the plate when it lands. */
static void claude_poll(lv_timer_t *t)
{
    (void)t;
    static bool was_busy;
    bool busy = claude_busy();
    if (busy && !was_busy) {
        ui_set_state(UI_STATE_THINKING);
    }
    was_busy = busy;
    cJSON *plate = claude_take_plate();
    if (plate) {
        ui_show_plate(plate, true);
        cJSON_Delete(plate);
    }
}

static void claude_bring_up(void)
{
    static tsr_settings_t st;   /* static: keeps the 400-byte struct off the stack */
    if (settings_load(&st) != ESP_OK) {
        ESP_LOGW(TAG, "Claude off: saved settings incomplete");
        return;
    }
    if (!settings_key_is_claude(&st)) {
        ESP_LOGW(TAG, "Claude off: the saved key isn't a Claude API key, so it isn't sent anywhere");
        return;
    }
    if (net_connect(st.wifi_ssid, st.wifi_password, 20000) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi not up yet; still retrying in the background");
    }
    memset(st.wifi_password, 0, sizeof(st.wifi_password));
    ESP_ERROR_CHECK(claude_start(st.api_key));
    memset(st.api_key, 0, sizeof(st.api_key));
    xTaskCreate(console_task, "console", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "ready: type a question into this console and press Enter");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Tessera screen-only build");
    board_dump_pins();   /* motor and amp, already held low by the constructor in board.c */
    lv_display_t *disp = display_start();

    ESP_ERROR_CHECK(board_touch_init(&s_touch));
    ESP_ERROR_CHECK(board_ai_button_init());
    ESP_ERROR_CHECK(mic_start());
    board_dump_pins();   /* and once more with the inputs configured */

    ui_create();
    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read);
    lv_indev_set_display(touch, disp);

    /* The deck starts with the example plates; the ring is shown first (swipe left for plates). */
    for (size_t i = 0; i < N_PLATES; i++) {
        cJSON *spec = cJSON_Parse(PLATES[i]);
        ui_show_plate(spec, false);
        cJSON_Delete(spec);
    }
    ui_set_state(UI_STATE_IDLE);

    lv_timer_create(ring_level, 33, NULL);
    lv_timer_create(ai_button_poll, 20, NULL);
    lv_timer_create(claude_poll, 50, NULL);
    lv_timer_handler();   /* draw the ring before the network comes up */
    claude_bring_up();

    while (1) {
        uint32_t wait = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(wait < 1 ? 1 : (wait > 10 ? 10 : wait)));
    }
}
