#pragma once

#include <stdbool.h>
#include <stdint.h>

// -- PAREDES / OBSTÁCULOS --
// El jugador que espera le tira piezas (tipo tetris) a la placa del otro.

#define NUM_SHAPES      7 // I, O, T, S, Z, J, L
#define CELLS_PER_SHAPE 4 // todas las piezas tienen 4 celdas, como en tetris
#define NUM_ROTATIONS   4 // 0, 90, 180 y 270 grados

// Nombres para las piezas, en el mismo orden que la tabla SHAPES de walls.c
typedef enum {
    SHAPE_I,
    SHAPE_O,
    SHAPE_T,
    SHAPE_S,
    SHAPE_Z,
    SHAPE_J,
    SHAPE_L,
} shape_id_t;

// Devuelve en `out` las 4 celdas de la pieza `shape` girada `rotation` veces
// 90 grados en sentido horario, relativas a su ancla (0, 0).
// Para saber dónde cae cada celda en la grilla: ancla + out[i].
void walls_shape_cells(shape_id_t shape, int rotation, int8_t out[CELLS_PER_SHAPE][2]);

// -- PAREDES EN EL TABLERO --
// Todas las posiciones son en CELDAS de la grilla (no píxeles).

#define MAX_WALLS        5  // si ya hay 5 y llega otra, se reemplaza la más vieja
#define WALL_ARM_FRAMES  15 // frames titilando antes de activarse (15 x 200ms = 3s)

// Le pregunta al juego "¿hay serpiente en esta celda?". La usa walls_update()
// para no activar una pared encima de la serpiente.
typedef bool (*walls_occupied_fn)(int gx, int gy);

// Borra todas las paredes (al empezar una partida)
void walls_reset(void);

// Agrega una pared nueva, que arranca titilando (todavía no hace daño)
void walls_add(shape_id_t shape, int rotation, int gx, int gy);

// Llamar una vez por frame: avanza los timers. Una pared que terminó de
// titilar se activa solo si `occupied` dice que no hay serpiente en ninguna
// de sus celdas; si hay, sigue titilando hasta que se libere.
void walls_update(walls_occupied_fn occupied);

// ¿Hay una pared ACTIVA en esta celda? (las que titilan no cuentan)
bool walls_hit(int gx, int gy);

// Dibuja las paredes: las activas fijas, las que se están armando titilando
void walls_render(void);

// -- PIEZAS PARA TIRAR (las gana el jugador que espera) --

#define MAX_PIECES 3 // cuántas piezas se pueden acumular sin usar

// El quiz/desafío llama a esto cuando el jugador gana una pieza
void walls_grant_piece(void);

// Cuántas piezas tiene para tirar ahora
int walls_pieces(void);

// Gasta una pieza. Devuelve false si no tenía ninguna.
bool walls_take_piece(void);

// La próxima pieza, como en tetris: salen las 7 en orden mezclado y recién
// cuando salieron todas se vuelve a mezclar (nunca 4 iguales seguidas).
shape_id_t walls_bag_next(void);
