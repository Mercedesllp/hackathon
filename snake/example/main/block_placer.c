#include <stdio.h>
#include "block_placer.h"
#include "walls.h"
#include "snake.h"
#include "radio.h"
#include "display.h"
#include "sounds.h"

#define MAX_ADV_WALLS 50

typedef struct {
    int cursor_x, cursor_y;
    int max_grid_x, max_grid_y;
    shape_id_t piece;
    shape_id_t next;
    int rotation;
    bool has_piece;

    struct {
        int x, y;
        shape_id_t shape;
        int rotation;
    } adv_walls[MAX_ADV_WALLS];
    int adv_walls_count;
} IdleState;

static IdleState idle;

static void clamp_cursor(void) {
    int8_t cells[CELLS_PER_SHAPE][2];
    walls_shape_cells(idle.piece, idle.rotation, cells);
    int min_dx = 0, max_dx = 0, min_dy = 0, max_dy = 0;
    for (int i = 0; i < CELLS_PER_SHAPE; i++) {
        if (cells[i][0] < min_dx) min_dx = cells[i][0];
        if (cells[i][0] > max_dx) max_dx = cells[i][0];
        if (cells[i][1] < min_dy) min_dy = cells[i][1];
        if (cells[i][1] > max_dy) max_dy = cells[i][1];
    }
    if (idle.cursor_x + min_dx < 1)                    idle.cursor_x = 1 - min_dx;
    if (idle.cursor_x + max_dx >= idle.max_grid_x - 1) idle.cursor_x = idle.max_grid_x - 2 - max_dx;
    if (idle.cursor_y + min_dy < 1)                    idle.cursor_y = 1 - min_dy;
    if (idle.cursor_y + max_dy >= idle.max_grid_y - 1) idle.cursor_y = idle.max_grid_y - 2 - max_dy;
}

void block_placer_reset(int max_grid_x, int max_grid_y) {
    idle.max_grid_x = max_grid_x;
    idle.max_grid_y = max_grid_y;
    idle.cursor_x = max_grid_x / 2;
    idle.cursor_y = max_grid_y / 2;
    idle.piece = walls_bag_next();
    idle.next = walls_bag_next();
    idle.rotation = 0;
    idle.has_piece = false;
    idle.adv_walls_count = 0;
}

bool block_placer_has_piece(void) {
    return idle.has_piece;
}

void block_placer_grant_piece(void) {
    idle.piece = idle.next;
    idle.next = walls_bag_next();
    idle.rotation = 0;
    idle.has_piece = true;
    clamp_cursor();
}

static void idle_drop_piece(bool audio_ok) {
    if (!idle.has_piece) return;

    if (idle.adv_walls_count < MAX_ADV_WALLS) {
        idle.adv_walls[idle.adv_walls_count].x = idle.cursor_x;
        idle.adv_walls[idle.adv_walls_count].y = idle.cursor_y;
        idle.adv_walls[idle.adv_walls_count].shape = idle.piece;
        idle.adv_walls[idle.adv_walls_count].rotation = idle.rotation;
        idle.adv_walls_count++;
    }

    message_t m = {
        .type = MSG_WALL,
        .x = idle.cursor_x,
        .y = idle.cursor_y,
        .shape = idle.piece,
        .rotation = idle.rotation,
    };
    radio_send(&m);
    if (audio_ok) wifi_audio_play_tone(990, 80);

    idle.has_piece = false;
    idle.rotation = 0;
}

void block_placer_handle_button(button_id_t id, bool audio_ok) {
    if (!idle.has_piece) return;

    switch (id) {
        case BTN_UP:    idle.cursor_x--; break;
        case BTN_RIGHT: idle.cursor_y--; break;
        case BTN_DOWN:  idle.cursor_x++; break;
        case BTN_LEFT:  idle.cursor_y++; break;
        case BTN_KEY2:  idle.rotation = (idle.rotation + 1) % NUM_ROTATIONS; break;
        case BTN_KEY3:  idle_drop_piece(audio_ok); return;
        default: break;
    }

    clamp_cursor();
}

void block_placer_render(void) {
    int8_t cells[CELLS_PER_SHAPE][2];

    display_text(3, 3, "K2:Rota K3:Tira", false);

    walls_shape_cells(idle.next, 0, cells);
    for (int i = 0; i < CELLS_PER_SHAPE; i++) {
        display_fill_rect(118 + cells[i][0] * 2, 5 + cells[i][1] * 2, 2, 2, true);
    }

    for (int w = 0; w < idle.adv_walls_count; w++) {
        walls_shape_cells(idle.adv_walls[w].shape, idle.adv_walls[w].rotation, cells);
        for (int i = 0; i < CELLS_PER_SHAPE; i++) {
            int gx = idle.adv_walls[w].x + cells[i][0];
            int gy = idle.adv_walls[w].y + cells[i][1];
            if (gx < 0 || gx >= idle.max_grid_x || gy < 0 || gy >= idle.max_grid_y) continue;
            display_draw_rect(WALL_SIZE + gx * SQUARE_SIZE, WALL_SIZE + gy * SQUARE_SIZE, SQUARE_SIZE, SQUARE_SIZE);
        }
    }

    if (idle.has_piece) {
        walls_shape_cells(idle.piece, idle.rotation, cells);
        for (int i = 0; i < CELLS_PER_SHAPE; i++) {
            int gx = idle.cursor_x + cells[i][0];
            int gy = idle.cursor_y + cells[i][1];
            if (gx < 0 || gx >= idle.max_grid_x || gy < 0 || gy >= idle.max_grid_y) continue;
            int px = WALL_SIZE + gx * SQUARE_SIZE;
            int py = WALL_SIZE + gy * SQUARE_SIZE;
            display_fill_rect(px, py, SQUARE_SIZE, SQUARE_SIZE, true);
        }
    }
}