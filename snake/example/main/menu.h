#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Muestra la pantalla de inicio, el menú de selección de modo y bloquea hasta confirmar.
esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok, bool audio_ok, int *selected_mode);

// Opciones de retorno del menú de pausa
typedef enum {
    PAUSE_CONTINUE = 0,
    PAUSE_QUIT = 1
} pause_choice_t;

// Muestra el menú de pausa y congela el juego hasta elegir una opción
pause_choice_t menu_pause(QueueHandle_t events, bool display_ok, bool audio_ok);