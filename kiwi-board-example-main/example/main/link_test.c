#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio.h"
#include "buttons.h"
#include "display.h"
#include "link.h"
#include "link_test.h"

static const char *TAG = "link_test";

#define UI_PERIOD_MS 50
// Resend our state periodically so the other board knows we're alive
// and catches up if a packet got lost.
#define LINK_HEARTBEAT_MS 200

static const int s_button_freq[BTN_COUNT] = {
    [BTN_UP] = 659,
    [BTN_DOWN] = 440,
    [BTN_LEFT] = 494,
    [BTN_RIGHT] = 587,
    [BTN_CENTER] = 784,
    [BTN_KEY1] = 262,
    [BTN_KEY2] = 330,
    [BTN_KEY3] = 392,
    [BTN_KEY4] = 523,
};

static void draw_key(int x, int y, int size, bool pressed)
{
    if (pressed) {
        display_fill_rect(x, y, size, size, true);
    } else {
        display_fill_rect(x, y, size, size, false);
        display_draw_rect(x, y, size, size);
    }
}

static void draw_ui(uint32_t pressed, const char *status)
{
    display_clear();
    display_text(2, 1, status, true);

    draw_key(15, 18, 10, pressed & (1u << BTN_UP));
    draw_key(4, 29, 10, pressed & (1u << BTN_LEFT));
    draw_key(15, 29, 10, pressed & (1u << BTN_CENTER));
    draw_key(26, 29, 10, pressed & (1u << BTN_RIGHT));
    draw_key(15, 40, 10, pressed & (1u << BTN_DOWN));

    display_text(50, 19, "KEYS", false);
    for (int i = 0; i < 4; i++) {
        bool on = pressed & (1u << (BTN_KEY1 + i));
        int x = 50 + i * 15;
        draw_key(x, 30, 13, on);
        char num[2] = {'1' + i, 0};
        display_text(x + 4, 33, num, on);
    }
}

static void show_error(bool display_ok, const char *line1, const char *line2)
{
    if (display_ok) {
        display_clear();
        display_text(2, 1, "LINK ERROR", true);
        display_text(2, 20, line1, false);
        display_text(2, 32, line2, false);
        display_flush();
    }
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}

void link_test_run(bool display_ok, bool audio_ok)
{
    ESP_LOGI(TAG, "link test mode");
    if (link_init() != ESP_OK) {
        show_error(display_ok, "Set Link secret", "in menuconfig");
    }

    QueueHandle_t events = buttons_event_queue();
    int heartbeat_div = 0;
    uint32_t prev_remote = 0;
    while (1) {
        button_event_t ev;
        bool changed = false;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(UI_PERIOD_MS)) == pdTRUE) {
            ESP_LOGI(TAG, "local %s %s", buttons_name(ev.id), ev.pressed ? "pressed" : "released");
            changed = true;
        }

        if (changed || ++heartbeat_div >= LINK_HEARTBEAT_MS / UI_PERIOD_MS) {
            heartbeat_div = 0;
            link_send_buttons(buttons_read());
        }

        uint32_t remote = link_remote_buttons();
        uint32_t just_pressed = remote & ~prev_remote;
        prev_remote = remote;
        for (int i = 0; i < BTN_COUNT; i++) {
            if (just_pressed & (1u << i)) {
                ESP_LOGI(TAG, "remote %s pressed", buttons_name(i));
                if (audio_ok) {
                    audio_play_tone(s_button_freq[i], 100);
                }
            }
        }

        if (display_ok) {
            draw_ui(remote, link_peer_alive() ? "REMOTE: CONNECTED" : "REMOTE: waiting..");
            display_flush();
        }
    }
}
