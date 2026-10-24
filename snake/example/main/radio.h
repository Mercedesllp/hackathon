#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "snake.h"   // message_t

// Comunicación entre las dos placas por ESP-NOW (sobre la radio WiFi, sin router).
// Los mensajes se mandan a todos (broadcast) en el canal 1.

// Prende la radio WiFi, inicia ESP-NOW y prepara el buzón de mensajes.
// Llamar una sola vez al arrancar, antes de usar radio_send / radio_receive.
esp_err_t radio_init(void);

// Manda un mensaje a la otra placa. Vuelve enseguida (no espera respuesta).
esp_err_t radio_send(const message_t *m);

// Mira el buzón: si llegó un mensaje lo copia en `m` y devuelve true.
// Si no hay nada devuelve false enseguida (no se queda esperando).
bool radio_receive(message_t *m);
