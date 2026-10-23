#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/queue.h"

esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok);
