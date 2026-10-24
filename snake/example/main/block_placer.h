#pragma once

#include <stdbool.h>
#include "buttons.h"

void block_placer_reset(int max_grid_x, int max_grid_y);
bool block_placer_has_piece(void);
void block_placer_grant_piece(void);
void block_placer_handle_button(button_id_t id, bool audio_ok);
void block_placer_render(void);