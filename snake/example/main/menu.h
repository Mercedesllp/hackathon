#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

// Constantes para el menú de pausa
#define PAUSE_RESUME 0
#define PAUSE_QUIT   1

// Inicia la pantalla de espera y devuelve el modo seleccionado (1 o 2)
esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok, bool audio_ok, int *selected_mode);

// Menú de pausa (devuelve PAUSE_RESUME o PAUSE_QUIT)
int menu_pause(QueueHandle_t events, bool display_ok, bool audio_ok);

// Menú interactivo para poner la pared en modo 1 Jugador o Multijugador
void menu_place_wall(QueueHandle_t events, bool display_ok, bool audio_ok, int *wall_x, int *wall_y, bool *is_vertical);