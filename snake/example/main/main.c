#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h" // Mejor generador aleatorio para la ESP32

#include "audio.h"
#include "buttons.h"
#include "display.h"
#include "board.h" 
#include "sdkconfig.h"

static const char *TAG = "demo";

void app_main(void)
{
    ESP_LOGI(TAG, "Hackathon board demo starting");

    bool display_ok = display_init() == ESP_OK;
    if (!display_ok) {
        ESP_LOGE(TAG, "display init failed - continuing without screen");
    }

    ESP_ERROR_CHECK(buttons_init());

    if (display_ok) {
        display_clear();
        display_text(10, 8, "Hello, Snake!", false);
        display_text(10, 24, "Iniciando...", false);
        display_flush();
    }

    bool audio_ok = audio_init() == ESP_OK;
    if (audio_ok) {
        const int notes[] = {523, 659, 784};
        for (int i = 0; i < 3; i++) {
            audio_play_tone(notes[i], 120);
        }
    } else {
        ESP_LOGE(TAG, "audio init failed - continuing without sound");
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    QueueHandle_t events = buttons_event_queue();

    // -- CONFIGURACIÓN DEL JUEGO --
    const int SQUARE_SIZE = 4;
    const int WALL_SIZE = 1;
    
    // Calculamos el tamaño del tablero en "grillas" de 4x4 para el spawn 
    int max_grid_x = (BOARD_LCD_WIDTH - 2 * WALL_SIZE) / SQUARE_SIZE;
    int max_grid_y = (BOARD_LCD_HEIGHT - 2 * WALL_SIZE) / SQUARE_SIZE;

    // Alinear la serpiente estrictamente a la grilla de 4x4
    int square_x = WALL_SIZE + (max_grid_x / 2) * SQUARE_SIZE; 
    int square_y = WALL_SIZE + (max_grid_y / 2) * SQUARE_SIZE;
    
    int dir_x = 1;
    int dir_y = 0;
    
    bool game_over = false; 

    // Variables de la Serpiente
    int score = 0; 
    const int MAX_TAIL = 200; // Límite máximo de la cola
    int tail_x[MAX_TAIL];
    int tail_y[MAX_TAIL];

    // Posición inicial de la manzana, alineada a la grilla
    int apple_x = WALL_SIZE + (esp_random() % max_grid_x) * SQUARE_SIZE;
    int apple_y = WALL_SIZE + (esp_random() % max_grid_y) * SQUARE_SIZE;

    const int GAME_SPEED_MS = 200;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        button_event_t ev;
        
        // Contadores de votos para este fotograma
        int votos_arriba = 0;
        int votos_abajo = 0;
        int votos_izq = 0;
        int votos_der = 0;
        
        // 1. LEER TODOS LOS INPUTS DEL FRAME
        while (xQueueReceive(events, &ev, 0) == pdTRUE) {
            if (ev.pressed) {
                if (game_over) {
                    // Resetear el juego
                    square_x = WALL_SIZE + (max_grid_x / 2) * SQUARE_SIZE;
                    square_y = WALL_SIZE + (max_grid_y / 2) * SQUARE_SIZE;
                    dir_x = 1;
                    dir_y = 0;
                    score = 0;
                    apple_x = WALL_SIZE + (esp_random() % max_grid_x) * SQUARE_SIZE;
                    apple_y = WALL_SIZE + (esp_random() % max_grid_y) * SQUARE_SIZE;
                    game_over = false;
                } else {
                    // Sumar votos según el botón presionado
                    switch (ev.id) {
                        // --- CONTROLES CON EL JOYSTICK ROTADO ---
                        case BTN_UP:    votos_izq++; break;
                        case BTN_RIGHT: votos_arriba++; break;
                        case BTN_DOWN:  votos_der++; break;
                        case BTN_LEFT:  votos_abajo++; break;

                        // --- CONTROLES CON LOS BOTONES EXTRA ---
                        case BTN_KEY4:  votos_arriba++; break;
                        case BTN_KEY1:  votos_izq++; break;
                        case BTN_KEY2:  votos_abajo++; break;
                        case BTN_KEY3:  votos_der++; break;
                        default: break;
                    }
                }
            }
        }

        // 2. PROCESAR EL RESULTADO DE LA VOTACIÓN
        if (!game_over) {
            // --- RESOLUCIÓN DE LA VOTACIÓN DE INPUTS ---
            int max_votos = 0;
            int nuevo_dir_x = dir_x;
            int nuevo_dir_y = dir_y;

            // Evaluamos al ganador descartando movimientos inválidos (180 grados)
            if (votos_arriba > max_votos && dir_y == 0) {
                max_votos = votos_arriba;
                nuevo_dir_x = 0; nuevo_dir_y = -1;
            }
            if (votos_abajo > max_votos && dir_y == 0) {
                max_votos = votos_abajo;
                nuevo_dir_x = 0; nuevo_dir_y = 1;
            }
            if (votos_izq > max_votos && dir_x == 0) {
                max_votos = votos_izq;
                nuevo_dir_x = -1; nuevo_dir_y = 0;
            }
            if (votos_der > max_votos && dir_x == 0) {
                max_votos = votos_der;
                nuevo_dir_x = 1; nuevo_dir_y = 0;
            }

            // Aplicar la dirección ganadora (si hubo al menos un voto válido)
            if (max_votos > 0) {
                dir_x = nuevo_dir_x;
                dir_y = nuevo_dir_y;
            }
            // -------------------------------------------

            // 1. Guardar la posición anterior para mover la cola
            int prev_x = square_x;
            int prev_y = square_y;

            // 2. Mover la cabeza
            square_x += dir_x * SQUARE_SIZE;
            square_y += dir_y * SQUARE_SIZE;

            // 3. Chequeo de paredes usando las coordenadas de la grilla
            int current_grid_x = (square_x - WALL_SIZE) / SQUARE_SIZE;
            int current_grid_y = (square_y - WALL_SIZE) / SQUARE_SIZE;

            if (current_grid_x < 0 || current_grid_x >= max_grid_x || 
                current_grid_y < 0 || current_grid_y >= max_grid_y) {
                
                game_over = true;
                if (audio_ok) audio_play_tone(150, 400); // Sonido grave al chocar
                
            } else {
                // 4. Chequear colisión con la manzana
                if (square_x == apple_x && square_y == apple_y) {
                    if (score < MAX_TAIL) score++; // Suma punto e incrementa cola
                    
                    if (audio_ok) audio_play_tone(880, 50); // "Beep" agudo de comer
                    
                    // Tirar una manzana nueva
                    apple_x = WALL_SIZE + (esp_random() % max_grid_x) * SQUARE_SIZE;
                    apple_y = WALL_SIZE + (esp_random() % max_grid_y) * SQUARE_SIZE;
                }

                // 5. Actualizar las posiciones de toda la cola
                for (int i = score - 1; i > 0; i--) {
                    tail_x[i] = tail_x[i-1];
                    tail_y[i] = tail_y[i-1];
                }
                if (score > 0) {
                    tail_x[0] = prev_x;
                    tail_y[0] = prev_y;
                }

                // 6. Colisión con sí mismo (te mordes la cola)
                for (int i = 0; i < score; i++) {
                    if (square_x == tail_x[i] && square_y == tail_y[i]) {
                        game_over = true;
                        if (audio_ok) audio_play_tone(150, 400);
                    }
                }
            }
        }

        // Renderizar en pantalla
        if (display_ok) {
            display_clear(); 
            
            // Paredes
            display_fill_rect(0, 0, BOARD_LCD_WIDTH, WALL_SIZE, true); 
            display_fill_rect(0, BOARD_LCD_HEIGHT - WALL_SIZE, BOARD_LCD_WIDTH, WALL_SIZE, true); 
            display_fill_rect(0, 0, WALL_SIZE, BOARD_LCD_HEIGHT, true); 
            display_fill_rect(BOARD_LCD_WIDTH - WALL_SIZE, 0, WALL_SIZE, BOARD_LCD_HEIGHT, true); 
            
            if (game_over) {
                display_text(37, 20, "GAME OVER", false); 
                
                // Mostrar puntaje final
                char final_score[16];
                snprintf(final_score, sizeof(final_score), "Puntos: %d", score);
                display_text(37, 36, final_score, false);

            } else {
                // Dibujar la Manzana (Cuerpo de 4x4, y apagamos 1x1 px en el medio para darle ese "punto")
                display_fill_rect(apple_x, apple_y, SQUARE_SIZE, SQUARE_SIZE, true); 
                display_fill_rect(apple_x + 1, apple_y + 1, 1, 1, false); 

                // Dibujar Cabeza del jugador
                display_fill_rect(square_x, square_y, SQUARE_SIZE, SQUARE_SIZE, true); 
                
                // Dibujar la Cola del jugador
                for (int i = 0; i < score; i++) {
                    display_fill_rect(tail_x[i], tail_y[i], SQUARE_SIZE, SQUARE_SIZE, true);
                }

                // Imprimir el puntaje actual arriba a la izquierda
                char score_str[16];
                snprintf(score_str, sizeof(score_str), "Pts:%d", score);
                display_text(4, 4, score_str, false);
            }
            
            display_flush(); 
        }

        // Espera para siguiente frame (200ms -> ~5 FPS. Podés bajarlo para más dificultad)
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GAME_SPEED_MS));
    }
}