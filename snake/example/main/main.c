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
    bool snake_is_here = true; // Empieza en la placa 1. La placa 2 debería arrancar en false.
    bool portals_open = false;

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
            // Chequear si se abren los portales
            portals_open = (score >= 2);

            if (snake_is_here) {
                // ... (Acá va tu código de leer los votos de dirección y actualizar prev_x, prev_y) ...
                
                // Mover la cabeza
                square_x += dir_x * SQUARE_SIZE;
                square_y += dir_y * SQUARE_SIZE;

                int current_grid_x = (square_x - WALL_SIZE) / SQUARE_SIZE;
                int current_grid_y = (square_y - WALL_SIZE) / SQUARE_SIZE;

                // Chequeo de bordes de pantalla
                if (current_grid_x < 0 || current_grid_x >= max_grid_x || 
                    current_grid_y < 0 || current_grid_y >= max_grid_y) {
                    
                    if (portals_open) {
                        // ¡MAGIA! La serpiente sale de tu pantalla.
                        snake_is_here = false;
                        
                        // TODO: ENVIAR DATOS A LA OTRA PLACA ACÁ
                        // Necesitás mandar: score (largo de cola), dir_x, dir_y, y la coordenada Y o X de salida.
                        
                        if (audio_ok) audio_play_tone(440, 150); // Sonido de warp
                    } else {
                        // Portales cerrados: chocaste contra la pared.
                        game_over = true;
                        if (audio_ok) audio_play_tone(150, 400); 
                    }
                } else {
                    // ... (Acá va tu código de comer manzana, mover la cola y chocar contra sí mismo) ...
                }
            } else {
                // ESTADO DE ESPERA: La serpiente está en la otra placa.
                // TODO: LEER DATOS DE LA OTRA PLACA ACÁ
                // if (recibi_mensaje_de_amigo) {
                //     snake_is_here = true;
                //     // Configurar square_x y square_y al borde opuesto de donde entró
                //     // Configurar dir_x, dir_y, score según lo recibido
                // }
            }
        }

        // Renderizar en pantalla
        // Renderizar en pantalla
        if (display_ok) {
            display_clear(); 
            
            // Dibujar paredes SOLO si los portales están cerrados
            if (!portals_open) {
                display_fill_rect(0, 0, BOARD_LCD_WIDTH, WALL_SIZE, true); 
                display_fill_rect(0, BOARD_LCD_HEIGHT - WALL_SIZE, BOARD_LCD_WIDTH, WALL_SIZE, true); 
                display_fill_rect(0, 0, WALL_SIZE, BOARD_LCD_HEIGHT, true); 
                display_fill_rect(BOARD_LCD_WIDTH - WALL_SIZE, 0, WALL_SIZE, BOARD_LCD_HEIGHT, true); 
            }
            
            if (game_over) {
                display_text(37, 20, "GAME OVER", false); 
                char final_score[16];
                snprintf(final_score, sizeof(final_score), "Puntos: %d", score);
                display_text(37, 36, final_score, false);
            } else if (snake_is_here) {
                // Solo renderizar manzanas y jugador si están en esta placa
                display_fill_rect(apple_x, apple_y, SQUARE_SIZE, SQUARE_SIZE, true); 
                display_fill_rect(apple_x + 1, apple_y + 1, 1, 1, false); 

                display_fill_rect(square_x, square_y, SQUARE_SIZE, SQUARE_SIZE, true); 
                for (int i = 0; i < score; i++) {
                    display_fill_rect(tail_x[i], tail_y[i], SQUARE_SIZE, SQUARE_SIZE, true);
                }
            } else {
                // Feedback visual de que estás esperando
                display_text(10, 28, "ESPERANDO A AMIGO", false);
            }
            
            display_flush(); 
        }

        // Espera para siguiente frame (200ms -> ~5 FPS. Podés bajarlo para más dificultad)
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GAME_SPEED_MS));
    }
}