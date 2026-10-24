bool menu_wait_for_friend(QueueHandle_t events, bool display_ok, bool audio_ok, bool *starts_with_snake) {
    if (display_ok) {
        display_clear();
        display_text(10, 28, "BUSCANDO AMIGO...", false);
        display_text(10, 44, "(Btn 1 para salir)", false);
        display_flush();
    }

    message_t basura;
    while (radio_receive(&basura)) {}
    button_event_t ev_basura;
    while (xQueueReceive(events, &ev_basura, 0)) {}

    TickType_t ultimo_aviso = 0;
    int mi_ticket = esp_random() % 10000;

    while (1) {
        if (xTaskGetTickCount() - ultimo_aviso > pdMS_TO_TICKS(300)) {
            message_t msg_ready = { .type = MSG_READY, .score = mi_ticket };
            radio_send(&msg_ready);
            ultimo_aviso = xTaskGetTickCount();
        }

        message_t msg_in;
        if (radio_receive(&msg_in) && msg_in.type == MSG_READY) {
            if (mi_ticket == msg_in.score) {
                mi_ticket = esp_random() % 10000;
                continue;
            }

            *starts_with_snake = (mi_ticket > msg_in.score);
            if (audio_ok) wifi_audio_play_tone(1046, 200);

            for (int i = 0; i < 3; i++) {
                message_t msg_ready_confirm = { .type = MSG_READY, .score = mi_ticket };
                radio_send(&msg_ready_confirm);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            return true;
        }

        button_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (ev.pressed && ev.id == BTN_KEY1) {
                return false;
            }
        }
    }
}