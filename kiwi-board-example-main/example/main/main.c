#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "audio.h"
#include "buttons.h"
#include "display.h"
#include "link_test.h"
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
        display_text(10, 8, "Hello, Hackathon!", false);
        display_text(10, 24, "ESP32-S3 dev board", false);
        display_text(10, 40, "Self test...", false);
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

#if CONFIG_HACK_LINK_TEST
    link_test_run(display_ok, audio_ok);
#endif

    QueueHandle_t events = buttons_event_queue();

    // Estado inicial
    const int SQUARE_SIZE = 4;
    const int WALL_SIZE = 1;
    int square_x = BOARD_LCD_WIDTH / 2; 
    int square_y = BOARD_LCD_HEIGHT / 2;
    
    int dir_x = 1;
    int dir_y = 0;
    
    bool game_over = false; 

    const int GAME_SPEED_MS = 200;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        button_event_t ev;
        
        // Leer inputs
        while (xQueueReceive(events, &ev, 0) == pdTRUE) {
            if (ev.pressed) {
                if (game_over) {
                    // Si perdiste, cualquier botón reinicia el juego
                    square_x = BOARD_LCD_WIDTH / 2;
                    square_y = BOARD_LCD_HEIGHT / 2;
                    dir_x = 1;
                    dir_y = 0;
                    game_over = false;
                } else {
                    // Joystick rotado arreglado
                    switch (ev.id) {
                        case BTN_UP:    
                            if (dir_x == 0) { dir_x = -1; dir_y = 0; } 
                            break;
                        case BTN_RIGHT: 
                            if (dir_y == 0) { dir_x = 0; dir_y = -1; } 
                            break;
                        case BTN_DOWN:  
                            if (dir_x == 0) { dir_x = 1; dir_y = 0; }  
                            break;
                        case BTN_LEFT:  
                            if (dir_y == 0) { dir_x = 0; dir_y = 1; } 
                            break;
                        default: 
                            break;
                    }
                }
            }
        }

        if (!game_over) {
            // Se mueve
            square_x += dir_x * SQUARE_SIZE;
            square_y += dir_y * SQUARE_SIZE;

            // Chequea si te moriste 
            if (square_x >= BOARD_LCD_WIDTH - SQUARE_SIZE || square_x < SQUARE_SIZE || 
                square_y >= BOARD_LCD_HEIGHT - SQUARE_SIZE || square_y < SQUARE_SIZE) {
                game_over = true;
            }
        }

        // Renderizar
        if (display_ok) {
            display_clear(); 
            
            // Paredes
            display_fill_rect(0, 0, BOARD_LCD_WIDTH, WALL_SIZE, true); 
            display_fill_rect(0, BOARD_LCD_HEIGHT - WALL_SIZE, BOARD_LCD_WIDTH, WALL_SIZE, true); 
            display_fill_rect(0, 0, WALL_SIZE, BOARD_LCD_HEIGHT, true); 
            display_fill_rect(BOARD_LCD_WIDTH - WALL_SIZE, 0, WALL_SIZE, BOARD_LCD_HEIGHT, true); 
            
            if (game_over) {
                display_text(37, 28, "GAME OVER", false); 
            } else {
                // Jugador
                display_fill_rect(square_x, square_y, SQUARE_SIZE, SQUARE_SIZE, true); 
            }
            
            display_flush(); 
        }

        // Espera para siguiente frame
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GAME_SPEED_MS));
    }
}