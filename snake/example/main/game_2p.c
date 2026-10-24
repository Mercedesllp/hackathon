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
static bool opponent_game_over = false;

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
                    // Mantenemos el puntaje propio de esta placa y sumamos lo que traía la serpiente si es necesario, 
                    // o asignamos el puntaje del contrincante si se prefiere separado. 
                    // Aquí conservamos el score que traía en su viaje:
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
                opponent_game_over = true;
                break;
            default: break;
        }
    }
}

static bool poll_and_check_abort(void) {
    receive_messages_2p();
    return s_game.snake_is_here || s_game.game_over || opponent_game_over;
}

// Renderizado personalizado para el final de multijugador (Victory o Game Over + Puntaje individual)
static void render_multiplayer_end(const GameState *g, bool is_winner) {
    display_clear();
    if (is_winner) {
        display_text(37, 16, "VICTORY", true);
    } else {
        display_text(31, 16, "GAME OVER", false);
    }
    char final_score[16];
    snprintf(final_score, sizeof(final_score), "Tus puntos: %d", g->score);
    display_text(16, 36, final_score, false);
    display_text(10, 52, "Btn 1 para salir", false);
}

void game_2p_run(QueueHandle_t events, bool display_ok, bool audio_ok) {
    bool starts_with_snake = true;
    if (!menu_wait_for_friend(events, display_ok, audio_ok, &starts_with_snake)) {
        return;
    }

    s_game.display_ok = display_ok;
    s_game.audio_ok = audio_ok;
    opponent_game_over = false;
    bool audio_played = false; // <--- Bandera para que el sonido suene una sola vez
    
    snake_reset_game(&s_game, true, starts_with_snake);
    block_placer_reset(s_game.max_grid_x, s_game.max_grid_y);

    bool back_to_menu = false;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (!back_to_menu) {
        // ... (tu código de lectura de botones y actualización del juego sigue igual) ...

        if (display_ok) {
            display_clear();
            
            if (s_game.game_over) {
                // Si perdimos nosotros
                if (!audio_played) {
                    if (audio_ok) sounds_play_game_over(); // <--- Suena una vez
                    audio_played = true;
                }
                render_multiplayer_end(&s_game, false);
            } else if (opponent_game_over) {
                // Si ganó el rival (nosotros ganamos)
                if (!audio_played) {
                    if (audio_ok) sounds_play_victory();   // <--- Suena una vez
                    audio_played = true;
                }
                render_multiplayer_end(&s_game, true);
            } else {
                snake_render_walls(&s_game);
                if (s_game.snake_is_here) {
                    walls_render();
                    snake_render_here(&s_game);
                } else {
                    block_placer_render();
                }
            }
            display_flush();
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GAME_SPEED_MS));
    }
}