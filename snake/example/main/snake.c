#include <stdio.h>
#include "esp_random.h"
#include "board.h"
#include "display.h"
#include "radio.h"
#include "sounds.h"
#include "snake.h"

#define GRID_W ((BOARD_LCD_WIDTH - 2 * WALL_SIZE) / SQUARE_SIZE)
#define GRID_H ((BOARD_LCD_HEIGHT - 2 * WALL_SIZE) / SQUARE_SIZE)

void snake_place_apple(GameState *g) {
    int gx, gy, intentos = 0;
    do {
        gx = esp_random() % g->max_grid_x;
        gy = esp_random() % g->max_grid_y;
    } while (walls_hit(gx, gy) && ++intentos < 100);
    g->apple_x = WALL_SIZE + gx * SQUARE_SIZE;
    g->apple_y = WALL_SIZE + gy * SQUARE_SIZE;
}

void snake_safe_walls_add(GameState *g, shape_id_t shape, int rotation, int x, int y) {
    walls_add(shape, rotation, x, y);

    int8_t cells[CELLS_PER_SHAPE][2];
    walls_shape_cells(shape, rotation, cells);

    int apple_gx = (g->apple_x - WALL_SIZE) / SQUARE_SIZE;
    int apple_gy = (g->apple_y - WALL_SIZE) / SQUARE_SIZE;

    for (int i = 0; i < CELLS_PER_SHAPE; i++) {
        int cell_gx = x + cells[i][0];
        int cell_gy = y + cells[i][1];
        if (cell_gx == apple_gx && cell_gy == apple_gy) {
            snake_place_apple(g);
            break;
        }
    }
}

void snake_reset_game(GameState *g, bool is_multiplayer, bool starts_with_snake) {
    g->max_grid_x = GRID_W;
    g->max_grid_y = GRID_H;
    g->is_multiplayer = is_multiplayer;

    g->x = WALL_SIZE + (g->max_grid_x / 2) * SQUARE_SIZE;
    g->y = WALL_SIZE + (g->max_grid_y / 2) * SQUARE_SIZE;
    g->dir_x = 1;
    g->dir_y = 0;
    g->score = 0;
    g->apples_eaten_here = 0;
    g->cola_visible = 0;

    g->game_over = false;
    g->game_over_notified = false;
    g->portals_open = false;
    g->wall_anim_step = 0;
    g->in_wall_menu = false;
    g->entry_steps_left = 0;

    g->snake_is_here = !is_multiplayer || starts_with_snake;
    walls_reset();
    snake_place_apple(g);
}

bool snake_on_cell(const GameState *g, int gx, int gy) {
    if (!g->snake_is_here) return false;
    int px = WALL_SIZE + gx * SQUARE_SIZE;
    int py = WALL_SIZE + gy * SQUARE_SIZE;
    if (g->x == px && g->y == py) return true;
    for (int i = 0; i < g->cola_visible; i++) {
        if (g->tail_x[i] == px && g->tail_y[i] == py) return true;
    }
    return false;
}

void snake_clear_votes(GameState *g) {
    g->votos_arriba = 0;
    g->votos_abajo = 0;
    g->votos_izq = 0;
    g->votos_der = 0;
}

void snake_vote_direction(GameState *g, button_id_t id) {
    switch (id) {
        case BTN_UP:    g->votos_izq++;    break;
        case BTN_RIGHT: g->votos_arriba++; break;
        case BTN_DOWN:  g->votos_der++;    break;
        case BTN_LEFT:  g->votos_abajo++;  break;
        default: break;
    }
}

static void resolve_new_direction(GameState *g) {
    int max_votos = 0;
    int nuevo_dir_x = g->dir_x, nuevo_dir_y = g->dir_y;

    if (g->votos_arriba > max_votos && g->dir_y == 0) { max_votos = g->votos_arriba; nuevo_dir_x = 0;  nuevo_dir_y = -1; }
    if (g->votos_abajo  > max_votos && g->dir_y == 0) { max_votos = g->votos_abajo;  nuevo_dir_x = 0;  nuevo_dir_y = 1;  }
    if (g->votos_izq    > max_votos && g->dir_x == 0) { max_votos = g->votos_izq;    nuevo_dir_x = -1; nuevo_dir_y = 0;  }
    if (g->votos_der    > max_votos && g->dir_x == 0) { max_votos = g->votos_der;    nuevo_dir_x = 1;  nuevo_dir_y = 0;  }

    if (max_votos > 0) {
        g->dir_x = nuevo_dir_x;
        g->dir_y = nuevo_dir_y;
    }
}

static void check_walls(GameState *g) {
    int gx = (g->x - WALL_SIZE) / SQUARE_SIZE;
    int gy = (g->y - WALL_SIZE) / SQUARE_SIZE;

    if (g->x < WALL_SIZE) gx = -1;
    if (g->y < WALL_SIZE) gy = -1;
    if (gx >= 0 && gx < g->max_grid_x && gy >= 0 && gy < g->max_grid_y) return;

    int w = BOARD_LCD_WIDTH, h = BOARD_LCD_HEIGHT, step = g->wall_anim_step;
    if (step > w / 2) step = w / 2;

    int horiz_w = (w / 2) - step, vert_h = (h / 2) - (step / 2);
    bool choca_pared = false;

    if (gx < 0 || gx >= g->max_grid_x) {
        if (g->y < vert_h || (g->y + SQUARE_SIZE) > (h / 2) + (step / 2)) choca_pared = true;
    } else if (gy < 0 || gy >= g->max_grid_y) {
        if (g->x < horiz_w || (g->x + SQUARE_SIZE) > (w / 2) + step) choca_pared = true;
    }

    if (g->entry_steps_left > 0) choca_pared = false;

    if (!choca_pared) {
        if (g->is_multiplayer) {
            g->snake_is_here = false;
            walls_grant_piece();
            message_t mensaje = { .type = MSG_SNAKE, .x = gx, .y = gy, .score = g->score };
            radio_send(&mensaje);
            if (g->audio_ok) wifi_audio_play_tone(440, 150);
        } else {
            g->in_wall_menu = true;
            g->apples_eaten_here = 0;
            if (gx < 0) g->x = WALL_SIZE + (g->max_grid_x - 1) * SQUARE_SIZE;
            else if (gx >= g->max_grid_x) g->x = WALL_SIZE;
            if (gy < 0) g->y = WALL_SIZE + (g->max_grid_y - 1) * SQUARE_SIZE;
            else if (gy >= g->max_grid_y) g->y = WALL_SIZE;
            if (g->audio_ok) wifi_audio_play_tone(440, 150);
        }
    } else {
        g->game_over = true;
        if (g->audio_ok) wifi_audio_play_tone(150, 400);
    }
}

void snake_update_portals(GameState *g) {
    if (g->entry_steps_left > 0) {
        g->portals_open = false;
    } else {
        g->portals_open = ((g->apples_eaten_here % 3) == 2);
    }

    if (g->portals_open) {
        if (g->wall_anim_step < 128) g->wall_anim_step += 16;
    } else {
        if (g->wall_anim_step > 0) g->wall_anim_step -= 16;
    }
}

void snake_step(GameState *g) {
    resolve_new_direction(g);
    int prev_x = g->x;
    int prev_y = g->y;

    g->x += g->dir_x * SQUARE_SIZE;
    g->y += g->dir_y * SQUARE_SIZE;

    if (g->entry_steps_left > 0) g->entry_steps_left--;

    check_walls(g);

    if (!g->game_over && g->snake_is_here) {
        int gx = (g->x - WALL_SIZE) / SQUARE_SIZE;
        int gy = (g->y - WALL_SIZE) / SQUARE_SIZE;
        if (walls_hit(gx, gy)) {
            g->game_over = true;
            if (g->audio_ok) wifi_audio_play_tone(150, 400);
        }
    }

    if (!g->game_over && g->snake_is_here) {
        // Colisión con manzana
        if (g->x == g->apple_x && g->y == g->apple_y) {
            if (g->score < MAX_TAIL) g->score++;
            g->apples_eaten_here++;
            if (g->audio_ok) wifi_audio_play_tone(880, 50);
            snake_place_apple(g);
        }

        // Actualizar cola
        if (g->cola_visible < g->score) g->cola_visible++;
        for (int i = g->cola_visible - 1; i > 0; i--) {
            g->tail_x[i] = g->tail_x[i - 1];
            g->tail_y[i] = g->tail_y[i - 1];
        }
        if (g->cola_visible > 0) {
            g->tail_x[0] = prev_x;
            g->tail_y[0] = prev_y;
        }

        // Colisión consigo misma
        for (int i = 0; i < g->cola_visible; i++) {
            if (g->x == g->tail_x[i] && g->y == g->tail_y[i]) {
                g->game_over = true;
                if (g->audio_ok) wifi_audio_play_tone(150, 400);
                break;
            }
        }
    }
}

void snake_render_walls(const GameState *g) {
    int w = BOARD_LCD_WIDTH, h = BOARD_LCD_HEIGHT, step = g->wall_anim_step;
    if (step > w / 2) step = w / 2;

    int horiz_w = (w / 2) - step, vert_h = (h / 2) - (step / 2);

    if (horiz_w > 0) {
        display_fill_rect(0, 0, horiz_w, WALL_SIZE, true);
        display_fill_rect((w / 2) + step, 0, horiz_w, WALL_SIZE, true);
        display_fill_rect(0, h - WALL_SIZE, horiz_w, WALL_SIZE, true);
        display_fill_rect((w / 2) + step, h - WALL_SIZE, horiz_w, WALL_SIZE, true);
    }
    if (vert_h > 0) {
        display_fill_rect(0, 0, WALL_SIZE, vert_h, true);
        display_fill_rect(0, (h / 2) + (step / 2), WALL_SIZE, vert_h, true);
        display_fill_rect(w - WALL_SIZE, 0, WALL_SIZE, vert_h, true);
        display_fill_rect(w - WALL_SIZE, (h / 2) + (step / 2), WALL_SIZE, vert_h, true);
    }

    if (g->entry_steps_left > 0) {
        int hole_x = g->entry_door_x;
        int hole_y = g->entry_door_y;
        int hole_w = SQUARE_SIZE;
        int hole_h = SQUARE_SIZE;
        int gap = 2;

        if (g->entry_dir_x != 0) {
            hole_w = WALL_SIZE;
            hole_h = SQUARE_SIZE + (gap * 2);
            hole_y -= gap;
            hole_x = (g->entry_dir_x == 1) ? 0 : (w - WALL_SIZE);
        } else {
            hole_h = WALL_SIZE;
            hole_w = SQUARE_SIZE + (gap * 2);
            hole_x -= gap;
            hole_y = (g->entry_dir_y == 1) ? 0 : (h - WALL_SIZE);
        }
        display_fill_rect(hole_x, hole_y, hole_w, hole_h, false);
    }
}

void snake_render_here(const GameState *g) {
    display_fill_rect(g->apple_x, g->apple_y, SQUARE_SIZE, SQUARE_SIZE, true);
    display_fill_rect(g->apple_x + 1, g->apple_y + 1, 1, 1, false);
    display_fill_rect(g->x, g->y, SQUARE_SIZE, SQUARE_SIZE, true);
    for (int i = 0; i < g->cola_visible; i++) {
        display_fill_rect(g->tail_x[i], g->tail_y[i], SQUARE_SIZE, SQUARE_SIZE, true);
    }
}

void snake_render_game_over(const GameState *g) {
    display_text(37, 20, "GAME OVER", false);
    char final_score[16];
    snprintf(final_score, sizeof(final_score), "Puntos: %d", g->score);
    display_text(37, 36, final_score, false);
}

bool snake_arrive(const message_t *m, int *head_x, int *head_y, int *dir_x, int *dir_y) {
    int entry_x, entry_y, dx, dy;

    if (m->x == -1) {
        entry_x = GRID_W - 1; entry_y = m->y; dx = -1; dy = 0;
    } else if (m->x == GRID_W) {
        entry_x = 0;          entry_y = m->y; dx = 1;  dy = 0;
    } else if (m->y == -1) {
        entry_x = m->x;       entry_y = GRID_H - 1; dx = 0; dy = -1;
    } else if (m->y == GRID_H) {
        entry_x = m->x;       entry_y = 0;          dx = 0; dy = 1;
    } else {
        return false;
    }

    int start_x = entry_x - dx;
    int start_y = entry_y - dy;

    *head_x = WALL_SIZE + start_x * SQUARE_SIZE;
    *head_y = WALL_SIZE + start_y * SQUARE_SIZE;
    *dir_x = dx;
    *dir_y = dy;
    return true;
}