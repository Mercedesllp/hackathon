#include "esp_log.h"
#include "display.h"
#include "buttons.h"
#include "sounds.h"
#include "radio.h"
#include "menu.h"
#include "game_1p.h"
#include "game_2p.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "Hackathon Snake starting");

    bool display_ok = (display_init() == ESP_OK);
    ESP_ERROR_CHECK(buttons_init());
    bool audio_ok = sounds_init();

    if (radio_init() != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al iniciar la radio");
    }

    if (audio_ok) {
        wifi_audio_play_tone(523, 120);
    }

    QueueHandle_t events = buttons_event_queue();

    while (1) {
        int selected_mode = 1;
        ESP_ERROR_CHECK(menu_wait_for_start(events, display_ok, audio_ok, &selected_mode));

        if (selected_mode == 1) {
            game_1p_run(events, display_ok, audio_ok);
        } else {
            game_2p_run(events, display_ok, audio_ok);
        }
    }
}