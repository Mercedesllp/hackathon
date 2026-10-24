#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#define PAUSE_RESUME 0
#define PAUSE_QUIT   1

esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok, bool audio_ok, int *selected_mode);
int menu_pause(QueueHandle_t events, bool display_ok, bool audio_ok);
void menu_place_wall(QueueHandle_t events, bool display_ok, bool audio_ok, int *wall_x, int *wall_y, bool *is_vertical);

// Sala de espera multijugador: devuelve true si encontró amigo (y asigna starts_with_snake),
// o false si el usuario apretó BTN_KEY1 para volver al menú principal.
bool menu_wait_for_friend(QueueHandle_t events, bool display_ok, bool audio_ok, bool *starts_with_snake);