#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"

#include "game_1p.h"
#include "snake.h"
#include "walls.h"
#include "menu.h"
#include "display.h"
#include "sounds.h"

static GameState s_game;

static bool is_occupied_1p(int gx, int gy) {
    return snake_on_cell(&s_game, gx, gy);
}

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
            } else if (ev.id == BTN_KEY4) {
                snake_safe_walls_add(&s_game, esp_random() % NUM_SHAPES, esp_random() % NUM_ROTATIONS,
                                     esp_random() % s_game.max_grid_x, esp_random() % s_game.max_grid_y);
            } else {
                snake_vote_direction(&s_game, ev.id);
            }
        }

        if (back_to_menu) break;

        if (s_game.in_wall_menu) {
            int nueva_pared_x, nueva_pared_y;
            bool nueva_pared_vert;
            menu_place_wall(events, display_ok, audio_ok, &nueva_pared_x, &nueva_pared_y, &nueva_pared_vert);
            s_game.in_wall_menu = false;

            s_game.entry_steps_left = s_game.score + 1;
            s_game.entry_door_x = s_game.x;
            s_game.entry_door_y = s_game.y;
            s_game.entry_dir_x = s_game.dir_x;
            s_game.entry_dir_y = s_game.dir_y;
            s_game.wall_anim_step = 0;

            if (audio_ok) wifi_audio_play_tone(660, 150);
            last_wake_time = xTaskGetTickCount();
            continue;
        }

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