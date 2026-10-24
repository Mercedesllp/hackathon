#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "buttons.h"
#include "walls.h"

#define START_LIVES   3
#define SQUARE_SIZE   4
#define WALL_SIZE     2
#define MAX_TAIL      200
#define GAME_SPEED_MS 200

typedef enum {
    MSG_READY = 1,
    MSG_SNAKE = 2,
    MSG_WALL  = 3,
    MSG_GAME_OVER = 4,
    MSG_POWER_SPEED = 5
} msg_type_t;

typedef struct __attribute__((packed)) message {
    uint8_t type;
    int8_t x, y;
    uint8_t shape;
    uint8_t rotation;
    uint16_t score;
} message_t;

typedef struct {
    int x, y;
    int dir_x, dir_y;
    int score;
    int apples_eaten_here;
    int cola_visible;
    int tail_x[MAX_TAIL];
    int tail_y[MAX_TAIL];
    int apple_x, apple_y;
    int lives; 
    int speed_boost_steps;

    bool game_over;
    bool game_over_notified;

    bool snake_is_here;
    bool portals_open;
    int wall_anim_step;
    bool in_wall_menu;

    int entry_steps_left;
    int entry_door_x;
    int entry_door_y;
    int entry_dir_x;
    int entry_dir_y;

    int max_grid_x;
    int max_grid_y;

    bool audio_ok;
    bool display_ok;

    int votos_arriba;
    int votos_abajo;
    int votos_izq;
    int votos_der;

    bool is_multiplayer;
} GameState;

void snake_reset_game(GameState *g, bool is_multiplayer, bool starts_with_snake);
void snake_place_apple(GameState *g);
void snake_safe_walls_add(GameState *g, shape_id_t shape, int rotation, int x, int y);
bool snake_on_cell(const GameState *g, int gx, int gy);
void snake_vote_direction(GameState *g, button_id_t id);
void snake_clear_votes(GameState *g);
void snake_update_portals(GameState *g);
void snake_step(GameState *g);

void snake_render_walls(const GameState *g);
void snake_render_here(const GameState *g);
void snake_render_game_over(const GameState *g);

bool snake_arrive(const message_t *m, int *head_x, int *head_y, int *dir_x, int *dir_y);