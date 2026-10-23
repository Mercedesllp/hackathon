#pragma once

#include <stdbool.h>

// Communication test: shows the OTHER board's buttons on this screen (the
// kiwi demo layout) and beeps when it presses one. Flash it on both boards.
// Never returns.
void link_test_run(bool display_ok, bool audio_ok);
