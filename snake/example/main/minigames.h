#pragma once

#include <stdbool.h>

// Callback que procesa mensajes de red mientras estás en un minijuego.
// Debe devolver true si el minijuego tiene que abortarse (ej: volvió la serpiente o hubo game over).
typedef bool (*minigame_abort_fn)(void);

bool minigames_run_quiz(bool display_ok, bool audio_ok, minigame_abort_fn should_abort);
bool minigames_run_math(bool display_ok, bool audio_ok, minigame_abort_fn should_abort);
bool minigames_run_fishing(bool display_ok, bool audio_ok, minigame_abort_fn should_abort);

// Elige uno de los 3 minijuegos al azar y lo ejecuta
bool minigames_run_random(bool display_ok, bool audio_ok, minigame_abort_fn should_abort);