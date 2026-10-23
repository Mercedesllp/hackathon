#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

#include "audio.h"
#include "buttons.h"
#include "display.h"
#include "board.h" 
#include "sdkconfig.h"

static const char *TAG = "demo";

// -- CONSTANTES DEL JUEGO --
#define SQUARE_SIZE 4
#define WALL_SIZE 1
#define MAX_TAIL 200
#define GAME_SPEED_MS 200

// -- ESTADO DEL JUEGO --
typedef struct {
    int x, y;
    int dir_x, dir_y;
    int score;
    int tail_x[MAX_TAIL];
    int tail_y[MAX_TAIL];
    int apple_x, apple_y;
    
    bool game_over;
    bool snake_is_here;
    bool portals_open;
    
    int max_grid_x;
    int max_grid_y;
    
    bool audio_ok;
    bool display_ok;
    
    int votos_arriba;
    int votos_abajo;
    int votos_izq;
    int votos_der;
} GameState;

static GameState game;

// -- FUNCIONES DE LÓGICA --

void reset_game(void) {
    game.x = WALL_SIZE + (game.max_grid_x / 2) * SQUARE_SIZE;
    game.y = WALL_SIZE + (game.max_grid_y / 2) * SQUARE_SIZE;
    game.dir_x = 1;
    game.dir_y = 0;
    game.score = 0;
    game.apple_x = WALL_SIZE + (esp_random() % game.max_grid_x) * SQUARE_SIZE;
    game.apple_y = WALL_SIZE + (esp_random() % game.max_grid_y) * SQUARE_SIZE;
    game.game_over = false;
    game.snake_is_here = true;
}

void process_inputs(QueueHandle_t events) {
    button_event_t ev;
    game.votos_arriba = 0;
    game.votos_abajo = 0;
    game.votos_izq = 0;
    game.votos_der = 0;

    while (xQueueReceive(events, &ev, 0) == pdTRUE) {
        if (ev.pressed) {
            if (game.game_over) {
                reset_game();
            } else {
                switch (ev.id) {
                    case BTN_UP:    game.votos_izq++; break;
                    case BTN_RIGHT: game.votos_arriba++; break;
                    case BTN_DOWN:  game.votos_der++; break;
                    case BTN_LEFT:  game.votos_abajo++; break;

                    case BTN_KEY4:  game.votos_arriba++; break;
                    case BTN_KEY1:  game.votos_izq++; break;
                    case BTN_KEY2:  game.votos_abajo++; break;
                    case BTN_KEY3:  game.votos_der++; break;
                    default: break;
                }
            }
        }
    }
}

void resolve_new_direction(void) {
    int max_votos = 0;
    int nuevo_dir_x = game.dir_x;
    int nuevo_dir_y = game.dir_y;

    if (game.votos_arriba > max_votos && game.dir_y == 0) { max_votos = game.votos_arriba; nuevo_dir_x = 0; nuevo_dir_y = -1; }
    if (game.votos_abajo > max_votos && game.dir_y == 0) { max_votos = game.votos_abajo; nuevo_dir_x = 0; nuevo_dir_y = 1; }
    if (game.votos_izq > max_votos && game.dir_x == 0) { max_votos = game.votos_izq; nuevo_dir_x = -1; nuevo_dir_y = 0; }
    if (game.votos_der > max_votos && game.dir_x == 0) { max_votos = game.votos_der; nuevo_dir_x = 1; nuevo_dir_y = 0; }

    if (max_votos > 0) {
        game.dir_x = nuevo_dir_x;
        game.dir_y = nuevo_dir_y;
    }
}

void check_walls(void) {
    int current_grid_x = (game.x - WALL_SIZE) / SQUARE_SIZE;
    int current_grid_y = (game.y - WALL_SIZE) / SQUARE_SIZE;

    if (current_grid_x < 0 || current_grid_x >= game.max_grid_x || 
        current_grid_y < 0 || current_grid_y >= game.max_grid_y) {
        
        if (game.portals_open) {
            game.snake_is_here = false;
            // TODO: ENVIAR DATOS (score, dir_x, dir_y, posición) POR I2C ACÁ
            if (game.audio_ok) audio_play_tone(440, 150);
        } else {
            game.game_over = true; 
            if (game.audio_ok) audio_play_tone(150, 400);
        }
    }
}

void check_apple_collision(void) {
    if (game.x == game.apple_x && game.y == game.apple_y) {
        if (game.score < MAX_TAIL) game.score++;
        if (game.audio_ok) audio_play_tone(880, 50);
        game.apple_x = WALL_SIZE + (esp_random() % game.max_grid_x) * SQUARE_SIZE;
        game.apple_y = WALL_SIZE + (esp_random() % game.max_grid_y) * SQUARE_SIZE;
    }
}

void update_tail_positions(int prev_x, int prev_y) {
    for (int i = game.score - 1; i > 0; i--) {
        game.tail_x[i] = game.tail_x[i-1];
        game.tail_y[i] = game.tail_y[i-1];
    }
    if (game.score > 0) {
        game.tail_x[0] = prev_x;
        game.tail_y[0] = prev_y;
    }
}

void check_self_collision(void) {
    for (int i = 0; i < game.score; i++) {
        if (game.x == game.tail_x[i] && game.y == game.tail_y[i]) {
            game.game_over = true;
            if (game.audio_ok) audio_play_tone(150, 400);
        }
    }
}

// -- FUNCIONES DE RENDERIZADO --

void render_walls(void) {
    display_fill_rect(0, 0, BOARD_LCD_WIDTH, WALL_SIZE, true);
    display_fill_rect(0, BOARD_LCD_HEIGHT - WALL_SIZE, BOARD_LCD_WIDTH, WALL_SIZE, true);
    display_fill_rect(0, 0, WALL_SIZE, BOARD_LCD_HEIGHT, true);
    display_fill_rect(BOARD_LCD_WIDTH - WALL_SIZE, 0, WALL_SIZE, BOARD_LCD_HEIGHT, true);
}

void render_game_over(void) {
    display_text(37, 20, "GAME OVER", false);
    char final_score[16];
    snprintf(final_score, sizeof(final_score), "Puntos: %d", game.score);
    display_text(37, 36, final_score, false);
}

void render_snake_here(void) {
    display_fill_rect(game.apple_x, game.apple_y, SQUARE_SIZE, SQUARE_SIZE, true);
    display_fill_rect(game.apple_x + 1, game.apple_y + 1, 1, 1, false);

    display_fill_rect(game.x, game.y, SQUARE_SIZE, SQUARE_SIZE, true);
    for (int i = 0; i < game.score; i++) {
        display_fill_rect(game.tail_x[i], game.tail_y[i], SQUARE_SIZE, SQUARE_SIZE, true);
    }
}

void render_waiting(void) {
    display_text(10, 28, "ESPERANDO A AMIGO", false);
}

// -- BUCLE PRINCIPAL --

void app_main(void)
{
    ESP_LOGI(TAG, "Hackathon board demo starting");

    game.display_ok = display_init() == ESP_OK;
    ESP_ERROR_CHECK(buttons_init());

    game.audio_ok = audio_init() == ESP_OK;
    if (game.audio_ok) {
        audio_play_tone(523, 120);
    }

    QueueHandle_t events = buttons_event_queue();

    // Inicializar variables calculadas
    game.max_grid_x = (BOARD_LCD_WIDTH - 2 * WALL_SIZE) / SQUARE_SIZE;
    game.max_grid_y = (BOARD_LCD_HEIGHT - 2 * WALL_SIZE) / SQUARE_SIZE;
    reset_game();

    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        process_inputs(events);

        if (!game.game_over) {
            game.portals_open = (game.score >= 2);

            if (game.snake_is_here) {
                resolve_new_direction();

                int prev_x = game.x;
                int prev_y = game.y;

                // Mover cabeza
                game.x += game.dir_x * SQUARE_SIZE;
                game.y += game.dir_y * SQUARE_SIZE;

                check_walls();

                if (!game.game_over && game.snake_is_here) {
                    check_apple_collision();
                    update_tail_positions(prev_x, prev_y);
                    check_self_collision();
                }
            } else {
                // TODO: LEER DATOS I2C ACÁ
                // Si la serpiente vuelve a esta placa:
                // game.snake_is_here = true;
            }
        }

        // Renderizado Orquestado
        if (game.display_ok) {
            display_clear();
            
            if (!game.portals_open) {
                render_walls();
            }
            
            if (game.game_over) {
                render_game_over();
            } else if (game.snake_is_here) {
                render_snake_here();
            } else {
                render_waiting();
            }
            
            display_flush();
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GAME_SPEED_MS));
    }
}