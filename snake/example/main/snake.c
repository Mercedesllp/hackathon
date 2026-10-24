#include <stdio.h>      // snprintf, para el texto del puntaje

#include "esp_random.h" // posiciones de la manzana

#include "audio.h"
#include "board.h"      // BOARD_LCD_WIDTH / BOARD_LCD_HEIGHT
#include "display.h"
#include "snake.h"

// Tamaño del tablero en celdas (mismo cálculo que max_grid_x / max_grid_y en main.c)
#define GRID_W ((BOARD_LCD_WIDTH - 2 * WALL_SIZE) / SQUARE_SIZE)
#define GRID_H ((BOARD_LCD_HEIGHT - 2 * WALL_SIZE) / SQUARE_SIZE)

// TODO: implementar las funciones declaradas en snake.h

bool snake_arrive(const message_t *m, int *head_x, int *head_y, int *dir_x, int *dir_y)
{
    int entry_x, entry_y; // celda por la que entra a ESTA pantalla
    int dx, dy;           // dirección

    // Paso 1: qué coordenada está fuera de rango -> por qué borde entra (pacman)
    if (m->x == -1) {               // salió por la izquierda -> entra por la derecha
        entry_x = GRID_W - 1; entry_y = m->y;
        dx = -1; dy = 0;
    } else if (m->x == GRID_W) {    // salió por la derecha -> entra por la izquierda
        entry_x = 0;          entry_y = m->y;
        dx = 1;  dy = 0;
    } else if (m->y == -1) {        // salió por arriba -> entra por abajo
        entry_x = m->x;       entry_y = GRID_H - 1;
        dx = 0;  dy = -1;
    } else if (m->y == GRID_H) {    // salió por abajo -> entra por arriba
        entry_x = m->x;       entry_y = 0;
        dx = 0;  dy = 1;
    } else {
        return false;               // ninguna coordenada fuera de rango: mensaje roto
    }

    // Paso 2: la cabeza va una celda ANTES de la entrada, porque el loop
    // primero mueve y después dibuja
    int start_x = entry_x - dx;
    int start_y = entry_y - dy;

    // Paso 3: de celdas a píxeles, como usa el resto del juego
    *head_x = WALL_SIZE + start_x * SQUARE_SIZE;
    *head_y = WALL_SIZE + start_y * SQUARE_SIZE;
    *dir_x = dx;
    *dir_y = dy;
    
    return true;
}