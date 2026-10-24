#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "game_2p.h"
#include "snake.h"
#include "walls.h"
#include "radio.h"
#include "menu.h"
#include "minigames.h"
#include "block_placer.h"
#include "display.h"
#include "sounds.h"

static GameState s_game;

static bool is_occupied_2p(int gx, int gy) {
    return snake_on_cell(&s_game, gx, gy);
}

static void receive_messages_2p(void) {
    message_t m;
    while (radio_receive(&m)) {
        switch (m.type) {
            case MSG_SNAKE:
                if (!s_game.snake_is_here && snake_arrive(&m, &s_game.x, &s_game.y, &s_game.dir_x, &s_game.dir_y)) {
                    s_game.snake_is_here = true;
                    s_game.score = m.score;
                    s_game.apples_eaten_here = 0;
                    s_game.cola_visible = 0;

                    s_game.entry_steps_left = s_game.score + 2;
                    s_game.entry_door_x = s_game.x;
                    s_game.entry_door_y = s_game.y;
                    s_game.entry_dir_x = s_game.dir_x;
                    s_game.entry_dir_y = s_game.dir_y;
                    s_game.wall_anim_step = 0;

                    if (s_game.audio_ok) wifi_audio_play_tone(660, 150);
                }
                break;
            case MSG_WALL:
                if (m.shape < NUM_SHAPES) {
                    snake_safe_walls_add(&s_game, m.shape, m.rotation, m.x, m.y);
                    if (s_game.audio_ok) wifi_audio_play_tone(330, 100);
                }
                break;
            case MSG_GAME_OVER:
                s_game.game_over = true;
                s_game.game_over_notified = true;
                if (s_game.audio_ok) wifi_audio_play_tone(150, 400);
                break;
            default: break;
        }
    }
}

// Callback para abortar minijuegos si llega la serpiente o termina la partida
static bool poll_and_check_abort(void) {
    receive_messages_2p();
    return s_game.snake_is_here || s_game.game_over;
}

void game_2p_run(QueueHandle_t events, bool display_ok, bool audio_ok) {
    bool starts_with_snake = true;
    if (!menu_wait_for_friend(events, display_ok, audio_ok, &starts_with_snake)) {
        return; // Volvió al menú con BTN_KEY1
    }

    s_game.display_ok = display_ok;
    s_game.audio_ok = audio_ok;
    snake_reset_game(&s_game, true, starts_with_snake);
    block_placer_reset(s_game.max_grid_x, s_game.max_grid_y);

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
            if (!s_game.snake_is_here) {
                block_placer_handle_button(ev.id, audio_ok);
            } else {
                snake_vote_direction(&s_game, ev.id);
            }
        }

        if (back_to_menu) break;

        if (!s_game.game_over) {
            receive_messages_2p();
            walls_update(is_occupied_2p);
            snake_update_portals(&s_game);

            if (s_game.snake_is_here) {
                snake_step(&s_game);
            } else {
                // Si no tenemos pieza para tirar, lanzamos un minijuego automáticamente
                if (!block_placer_has_piece()) {
                    bool won = minigames_run_random(display_ok, audio_ok, poll_and_check_abort);
                    if (won) {
                        block_placer_grant_piece();
                    }
                    last_wake_time = xTaskGetTickCount();
                }
            }
        }

        if (s_game.game_over && !s_game.game_over_notified) {
            s_game.game_over_notified = true;
            message_t msg_muerte = { .type = MSG_GAME_OVER };
            radio_send(&msg_muerte);
        }

        if (display_ok) {
            display_clear();
            snake_render_walls(&s_game);

            if (s_game.game_over) {
                snake_render_game_over(&s_game);
            } else if (s_game.snake_is_here) {
                walls_render();
                snake_render_here(&s_game);
            } else {
                block_placer_render();
            }
            display_flush();
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GAME_SPEED_MS));
    }
}