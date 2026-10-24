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
            case MSG_POWER_SPEED:
                s_game.speed_boost_steps = 30; // 30 pasos a toda velocidad (aprox 3 segundos)
                if (s_game.audio_ok) sounds_play_tone(1200, 200); // Sonido agudo de alerta
                break;
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

static int choose_powerup(QueueHandle_t events, bool display_ok, bool audio_ok) {
    int selection = 0; // 0 = Pared, 1 = Velocidad
    button_event_t ev;

    // Limpiamos la cola de botones por si quedó algo apretado del minijuego
    while (xQueueReceive(events, &ev, 0) == pdTRUE) {}

    while (1) {
        // Si llega la serpiente o alguien muere mientras elegimos, abortamos
        if (poll_and_check_abort()) return -1;

        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (ev.pressed) {
                if (ev.id == BTN_UP || ev.id == BTN_DOWN) {
                    selection = !selection;
                    if (audio_ok) sounds_play_tone(440, 50); // Sonido cursor
                } else if (ev.id == BTN_KEY1 || ev.id == BTN_KEY2) {
                    if (audio_ok) sounds_play_tone(1046, 150); // Sonido confirmar
                    return selection;
                }
            }
        }

        if (display_ok) {
            display_clear();
            display_text(30, 8, "PODER LISTO", false);
            
            if (selection == 0) display_text(15, 30, "> MANDAR PARED <", false);
            else                display_text(27, 30, "MANDAR PARED", false);

            if (selection == 1) display_text(15, 46, "> RAYO VELOZ <", false);
            else                display_text(27, 46, "RAYO VELOZ", false);
            
            display_flush();
        }
    }
}

void game_2p_run(QueueHandle_t events, bool display_ok, bool audio_ok) {
    bool starts_with_snake = true;
    if (!menu_wait_for_friend(events, display_ok, audio_ok, &starts_with_snake)) {
        return;
    }

    s_game.display_ok = display_ok;
    s_game.audio_ok = audio_ok;
    opponent_game_over = false;
    snake_reset_game(&s_game, true, starts_with_snake);
    block_placer_reset(s_game.max_grid_x, s_game.max_grid_y);

    bool back_to_menu = false;
    TickType_t last_wake_time = xTaskGetTickCount();

    button_id_t held_btn = 0;
    bool is_held = false;

    while (!back_to_menu) {
        button_event_t ev;
        snake_clear_votes(&s_game);

        // 1. PROCESAMOS LOS BOTONES (Ahora miramos también cuando se sueltan)
        while (xQueueReceive(events, &ev, 0) == pdTRUE) {
            if (s_game.game_over || opponent_game_over) {
                if (ev.pressed && ev.id == BTN_KEY1) back_to_menu = true;
                continue;
            }

            if (!s_game.snake_is_here) {
                // Si es un direccional, guardamos el estado (apretado/suelto)
                if (ev.id == BTN_UP || ev.id == BTN_DOWN || ev.id == BTN_LEFT || ev.id == BTN_RIGHT) {
                    if (ev.pressed) {
                        held_btn = ev.id;
                        is_held = true;
                    } else if (ev.id == held_btn) {
                        is_held = false;
                    }
                }
                
                // Mueve la pieza al tocar el botón por primera vez
                if (ev.pressed) {
                    block_placer_handle_button(ev.id, audio_ok);
                }
            } else {
                is_held = false; // Reset por si llegó la víbora mientras manteníamos apretado
                if (ev.pressed) {
                    snake_vote_direction(&s_game, ev.id);
                }
            }
        }

        if (back_to_menu) break;

        if (!s_game.game_over && !opponent_game_over) {
            receive_messages_2p();
            walls_update(is_occupied_2p);
            snake_update_portals(&s_game);

            if (s_game.snake_is_here) {
                snake_step(&s_game);
            } else {
                // 2. APLICAMOS EL MOVIMIENTO CONTINUO
                if (is_held) {
                    // Le mandamos 'false' en el audio para que no te ametralle a pitidos mientras mantenés
                    block_placer_handle_button(held_btn, false);
                }

                // ... (Acá abajo sigue igual con tu lógica de minijuegos y el choose_powerup) ...
                if (!block_placer_has_piece()) {
                    bool won = minigames_run_random(display_ok, audio_ok, poll_and_check_abort);
                    if (won) {
                        int choice = choose_powerup(events, display_ok, audio_ok);
                        
                        if (choice == 0) {
                            // Eligió la pared: entra al block_placer
                            block_placer_grant_piece();
                        } else if (choice == 1) {
                            // Eligió la velocidad: la manda directo por radio
                            message_t msg = { .type = MSG_POWER_SPEED };
                            radio_send(&msg);
                        }
                        // Si choice es -1, significa que llegó la serpiente y se canceló el menú
                    }
                    last_wake_time = xTaskGetTickCount();
                }
            }
        }

        // Si nosotros morimos, avisamos a la otra placa
        if (s_game.game_over && !s_game.game_over_notified) {
            s_game.game_over_notified = true;
            message_t msg_muerte = { .type = MSG_GAME_OVER };
            radio_send(&msg_muerte);
        }

        if (display_ok) {
            display_clear();
            
            if (s_game.game_over) {
                // Si nosotros perdimos -> Game Over para nosotros
                render_multiplayer_end(&s_game, false);
            } else if (opponent_game_over) {
                // Si el rival perdió primero -> Victory para nosotros
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

        int current_speed = (s_game.speed_boost_steps > 0) ? (GAME_SPEED_MS / 2) : GAME_SPEED_MS;
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(current_speed));
    }
}