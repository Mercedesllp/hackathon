#include <string.h>

#include "esp_random.h"
#include "board.h"   // BOARD_LCD_WIDTH / BOARD_LCD_HEIGHT
#include "display.h"
#include "snake.h"   // SQUARE_SIZE, WALL_SIZE
#include "walls.h"

// Tamaño del tablero en celdas (mismo cálculo que max_grid_x / max_grid_y)
#define GRID_W ((BOARD_LCD_WIDTH - 2 * WALL_SIZE) / SQUARE_SIZE)
#define GRID_H ((BOARD_LCD_HEIGHT - 2 * WALL_SIZE) / SQUARE_SIZE)

// Cada pieza es una lista de celdas (x, y) relativas a su ancla (0, 0).
// El ancla va cerca del medio de la pieza, porque la rotación gira alrededor
// de (0, 0). Ojo: y crece hacia ABAJO, como en la pantalla.
// En los dibujos, (0,0) es la celda marcada con @.
//
// [pieza][celda][0 = x, 1 = y]
static const int8_t SHAPES[NUM_SHAPES][CELLS_PER_SHAPE][2] = {
    // I   ■ @ ■ ■          4 en fila: no hay celda del medio, elegimos la 2da
    [SHAPE_I] = { {-1, 0}, {0, 0}, {1, 0}, {2, 0} },

    // O   @ ■
    //     ■ ■
    [SHAPE_O] = { {0, 0}, {1, 0}, {0, 1}, {1, 1} },

    // T   ■ @ ■
    //       ■
    [SHAPE_T] = { {-1, 0}, {0, 0}, {1, 0}, {0, 1} },

    // S     @ ■
    //     ■ ■
    [SHAPE_S] = { {0, 0}, {1, 0}, {-1, 1}, {0, 1} },

    // Z   ■ @
    //       ■ ■
    [SHAPE_Z] = { {-1, 0}, {0, 0}, {0, 1}, {1, 1} },

    // J   ■
    //     ■ @ ■
    [SHAPE_J] = { {-1, -1}, {-1, 0}, {0, 0}, {1, 0} },

    // L       ■
    //     ■ @ ■
    [SHAPE_L] = { {1, -1}, {-1, 0}, {0, 0}, {1, 0} },
};

void walls_shape_cells(shape_id_t shape, int rotation, int8_t out[CELLS_PER_SHAPE][2])
{
    // El cuadrado se ve igual girado, pero como su ancla está en una esquina
    // girarlo lo correría de lugar. Así que no lo giramos.
    if (shape == SHAPE_O) {
        rotation = 0;
    }
    rotation %= NUM_ROTATIONS;

    for (int i = 0; i < CELLS_PER_SHAPE; i++) {
        int x = SHAPES[shape][i][0];
        int y = SHAPES[shape][i][1];

        // Girar 90 grados horario alrededor de (0,0): (x, y) -> (-y, x)
        // (con y para abajo: derecha -> abajo -> izquierda -> arriba)
        for (int r = 0; r < rotation; r++) {
            int nuevo_x = -y;
            int nuevo_y = x;
            x = nuevo_x;
            y = nuevo_y;
        }
        out[i][0] = x;
        out[i][1] = y;
    }
}

// -- PAREDES EN EL TABLERO --

typedef struct {
    bool used;        // ¿este lugar del array tiene una pared?
    bool live;        // false = titilando (no hace daño), true = activa
    shape_id_t shape;
    int rotation;
    int gx, gy;       // ancla, en celdas
    int timer;        // frames que faltan para terminar de titilar
} wall_t;

static wall_t s_walls[MAX_WALLS];

// Buffer circular: el próximo lugar a usar. Va 0, 1, ... MAX_WALLS-1, 0, 1...
// Cuando el array está lleno, ese lugar es justo el de la pared más vieja.
static int s_next;

// Cuenta frames para el titileo (no importa que dé la vuelta)
static unsigned s_frame;

// Piezas ganadas y todavía no tiradas
static int s_pieces;

// La "bolsa" de piezas: las 7 mezcladas; s_bag_pos es la próxima a sacar
static shape_id_t s_bag[NUM_SHAPES];
static int s_bag_pos = NUM_SHAPES; // = bolsa vacía, se llena en el primer uso

void walls_reset(void)
{
    memset(s_walls, 0, sizeof(s_walls));
    s_next = 0;
    s_pieces = 0;
    s_bag_pos = NUM_SHAPES;
}

void walls_grant_piece(void)
{
    if (s_pieces < MAX_PIECES) {
        s_pieces++;
    }
}

int walls_pieces(void)
{
    return s_pieces;
}

bool walls_take_piece(void)
{
    if (s_pieces == 0) {
        return false;
    }
    s_pieces--;
    return true;
}

shape_id_t walls_bag_next(void)
{
    if (s_bag_pos >= NUM_SHAPES) {
        // Bolsa vacía: meter las 7 y mezclarlas (Fisher-Yates: cada posición,
        // de atrás para adelante, se cambia con una al azar de las anteriores)
        for (int i = 0; i < NUM_SHAPES; i++) {
            s_bag[i] = (shape_id_t)i;
        }
        for (int i = NUM_SHAPES - 1; i > 0; i--) {
            int j = esp_random() % (i + 1);
            shape_id_t tmp = s_bag[i];
            s_bag[i] = s_bag[j];
            s_bag[j] = tmp;
        }
        s_bag_pos = 0;
    }
    return s_bag[s_bag_pos++];
}

void walls_add(shape_id_t shape, int rotation, int gx, int gy)
{
    s_walls[s_next] = (wall_t){
        .used = true,
        .live = false,
        .shape = shape,
        .rotation = rotation,
        .gx = gx,
        .gy = gy,
        .timer = WALL_ARM_FRAMES,
    };
    s_next = (s_next + 1) % MAX_WALLS;
}

// Celdas de la pared ya ubicadas en la grilla (ancla + forma girada)
static void wall_cells(const wall_t *w, int cells[CELLS_PER_SHAPE][2])
{
    int8_t rel[CELLS_PER_SHAPE][2];
    walls_shape_cells(w->shape, w->rotation, rel);
    for (int i = 0; i < CELLS_PER_SHAPE; i++) {
        cells[i][0] = w->gx + rel[i][0];
        cells[i][1] = w->gy + rel[i][1];
    }
}

static bool in_grid(int gx, int gy)
{
    return gx >= 0 && gx < GRID_W && gy >= 0 && gy < GRID_H;
}

void walls_update(walls_occupied_fn occupied)
{
    s_frame++;

    for (int w = 0; w < MAX_WALLS; w++) {
        wall_t *wall = &s_walls[w];
        if (!wall->used || wall->live) {
            continue;
        }
        if (wall->timer > 0) {
            wall->timer--;
            continue;
        }

        // Terminó de titilar: solo se activa si la serpiente no está encima
        int cells[CELLS_PER_SHAPE][2];
        wall_cells(wall, cells);
        bool blocked = false;
        for (int i = 0; i < CELLS_PER_SHAPE; i++) {
            if (occupied(cells[i][0], cells[i][1])) {
                blocked = true;
                break;
            }
        }
        if (!blocked) {
            wall->live = true;
        }
    }
}

bool walls_hit(int gx, int gy)
{
    for (int w = 0; w < MAX_WALLS; w++) {
        if (!s_walls[w].used || !s_walls[w].live) {
            continue;
        }
        int cells[CELLS_PER_SHAPE][2];
        wall_cells(&s_walls[w], cells);
        for (int i = 0; i < CELLS_PER_SHAPE; i++) {
            if (cells[i][0] == gx && cells[i][1] == gy) {
                return true;
            }
        }
    }
    return false;
}

void walls_render(void)
{
    // Las que se están armando se dibujan 2 frames sí, 2 frames no (titileo)
    bool blink_on = (s_frame / 2) % 2 == 0;

    for (int w = 0; w < MAX_WALLS; w++) {
        const wall_t *wall = &s_walls[w];
        if (!wall->used || (!wall->live && !blink_on)) {
            continue;
        }
        int cells[CELLS_PER_SHAPE][2];
        wall_cells(wall, cells);
        for (int i = 0; i < CELLS_PER_SHAPE; i++) {
            if (!in_grid(cells[i][0], cells[i][1])) {
                continue; // la parte de la pieza que cae afuera del tablero no existe
            }
            int px = WALL_SIZE + cells[i][0] * SQUARE_SIZE;
            int py = WALL_SIZE + cells[i][1] * SQUARE_SIZE;
            if (wall->live) {
                // Activa: cuadrado lleno con una diagonal apagada, para que no se
                // confunda con la cola (llena) ni con la manzana (un solo punto)
                display_fill_rect(px, py, SQUARE_SIZE, SQUARE_SIZE, true);
                display_set_pixel(px + 1, py + 1, false);
                display_set_pixel(px + 2, py + 2, false);
            } else {
                // Titilando: solo el borde
                display_draw_rect(px, py, SQUARE_SIZE, SQUARE_SIZE);
            }
        }
    }
}
