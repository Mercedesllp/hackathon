#pragma once

#include <stdbool.h>

// Inicializa el servidor Wi-Fi Audio (AP) y el hardware I2S (parlante + micrófono)
bool sounds_init(void);

// Reproduce un tono en el celular (Wi-Fi) y/o en el parlante de la placa
void sounds_play_tone(int freq_hz, int ms);

// Alias para mantener compatibilidad con llamadas existentes
void wifi_audio_play_tone(int freq_hz, int ms);