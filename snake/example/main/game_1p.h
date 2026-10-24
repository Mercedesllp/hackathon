#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void game_1p_run(QueueHandle_t events, bool display_ok, bool audio_ok);