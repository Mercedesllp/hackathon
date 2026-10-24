void game_1p_run(QueueHandle_t events, bool display_ok, bool audio_ok) {
    s_game.display_ok = display_ok;
    s_game.audio_ok = audio_ok;
    snake_reset_game(&s_game, false, true);

    bool back_to_menu = false;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (!back_to_menu) {
        button_event_t ev;
        snake_clear_votes(&s_game);

        while (xQueueReceive(events, &ev, 0) == pdTRUE) {
            if (!ev.pressed) continue;
            if (s_game.game_over) {
                back_to_menu = true;
                continue;
            }

            if (ev.id == BTN_KEY1) {
                if (menu_pause(events, display_ok, audio_ok) == PAUSE_QUIT) {
                    back_to_menu = true;
                }
            } else {
                snake_vote_direction(&s_game, ev.id);
            }
        }

        if (back_to_menu) break;

        if (!s_game.game_over) {
            walls_update(is_occupied_1p);
            snake_update_portals(&s_game);
            snake_step(&s_game);
        }

        if (display_ok) {
            display_clear();
            snake_render_walls(&s_game);
            if (s_game.game_over) {
                snake_render_game_over(&s_game);
            } else {
                walls_render();
                snake_render_here(&s_game);
            }
            display_flush();
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GAME_SPEED_MS));
    }
}