#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Muestra la pantalla de inicio y bloquea hasta que se presione un botón.
esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok);