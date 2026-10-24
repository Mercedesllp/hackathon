#pragma once

#include <stdbool.h>
#include <stdint.h>

// -- CONFIGURACIÓN DEL JUEGO --
// Son #define (y no const int) porque los vamos a usar para dimensionar
// arrays adentro de structs, y eso necesita constantes de compilación.
#define SQUARE_SIZE 4
#define WALL_SIZE   1
#define MAX_TAIL    200 // Límite máximo de la cola

typedef struct message {
    int8_t x, y;
    uint16_t score;
} message_t;


// TODO: struct con todo el estado del juego (lo que hoy son variables locales de app_main)

// TODO: declarar las funciones (reset, avanzar un frame, llegada de la serpiente, dibujar)

// La serpiente llega desde la otra placa.
// `m` trae la posición de la cabeza (en celdas) justo después de salir de la
// otra placa: una de las coordenadas está fuera de rango y eso dice por qué
// borde entra acá (regla pacman).
// Devuelve en píxeles dónde poner la cabeza (una celda ANTES del borde, así el
// próximo movimiento la deja justo en la celda de entrada) y la dirección.
// Devuelve false si el mensaje no tiene sentido (y no toca nada).
bool snake_arrive(const message_t *m, int *head_x, int *head_y, int *dir_x, int *dir_y);