#include "menu.h"

#include "display.h"
#include "buttons.h"
#include "board.h"

#define BLINK_MS 500

static void draw_start(bool visible)
{
    display_clear();
    display_text(49, 12, "SNAKE", false);
    if (visible) {
        display_text(49, 36, "START", false);
    }
    display_flush();
}

esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok)
{
    button_event_t ev;

    // Descarta eventos viejos
    while (xQueueReceive(events, &ev, 0) == pdTRUE) {}

    bool visible = true;
    while (1) {
        if (display_ok) {
            draw_start(visible);
        }

        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(BLINK_MS)) == pdTRUE) {
            if (ev.pressed) {
                return ESP_OK;
            }
        } else {
            visible = !visible;
        }
    }
}