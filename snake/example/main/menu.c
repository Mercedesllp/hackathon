#include "menu.h"
#include "display.h"
#include "buttons.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Declaramos la función que tenés en el main.c para poder usarla acá
extern void wifi_audio_play_tone(int freq_hz, int ms);

#define BLINK_MS 500

static void draw_start(bool visible)
{
    display_clear();
    display_text(49, 12, "SNAKE", false);
    if (visible) {
        display_text(49, 36, "START", false);
    }
    display_flush();
}

esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok, bool audio_ok)
{
    button_event_t ev;

    // --- ANIMACIÓN DE ENTRADA ---
    if (display_ok) {
        const int anim_frames = 20;
        const int anim_delay = 25;
        for (int step = 0; step <= anim_frames; step++) {
            display_clear();
            int s_x  = 0 + (49 - 0) * step / anim_frames;
            int t1_y = 64 + (36 - 64) * step / anim_frames;
            int a_y  = 0 + (36 - 0) * step / anim_frames;
            int r_y  = 64 + (36 - 64) * step / anim_frames;
            int t2_x = 120 + (73 - 120) * step / anim_frames;

            display_text(s_x, 36, "S", false);
            display_text(55, t1_y, "T", false);
            display_text(61, a_y,  "A", false);
            display_text(67, r_y,  "R", false);
            display_text(t2_x, 36, "T", false);
            display_text(49, 12, "SNAKE", false);
            
            display_flush();
            vTaskDelay(pdMS_TO_TICKS(anim_delay));
        }
    }
    
    // Descartar botones que se hayan apretado durante la animación
    while (xQueueReceive(events, &ev, 0) == pdTRUE) {}

    // --- MUSIQUITA DE ESPERA ---
    // Un arpegio "Arcade" de 6 notas
    const int notes[] = {262, 330, 392, 523, 392, 330}; 
    const int note_duration = 125; // 125ms = ritmo rápido y alegre
    int num_notes = 6;
    int note_idx = 0;
    
    int time_to_blink = 0; // Forzamos a que dibuje al arrancar
    bool visible = false;

    while (1) {
        // 1. Manejo del parpadeo visual (independiente de la música)
        if (time_to_blink <= 0) {
            visible = !visible;
            if (display_ok) draw_start(visible);
            time_to_blink = BLINK_MS;
        }

        // 2. Tocar la nota actual
        if (audio_ok) {
            wifi_audio_play_tone(notes[note_idx], note_duration);
        }

        // 3. Esperar a que termine la nota, leyendo si se apretó "START"
        TickType_t start_tick = xTaskGetTickCount();
        TickType_t ticks_to_wait = pdMS_TO_TICKS(note_duration);
        
        while (ticks_to_wait > 0) {
            if (xQueueReceive(events, &ev, ticks_to_wait) == pdTRUE) {
                if (ev.pressed) {
                    // Sonido agudo confirmando el START
                    if (audio_ok) wifi_audio_play_tone(1046, 400);
                    return ESP_OK;
                }
                
                // Si alguien tocó y soltó un botón inválido, recalculamos cuánto falta
                // para que la música no pierda el ritmo.
                TickType_t elapsed = xTaskGetTickCount() - start_tick;
                if (elapsed < pdMS_TO_TICKS(note_duration)) {
                    ticks_to_wait = pdMS_TO_TICKS(note_duration) - elapsed;
                } else {
                    ticks_to_wait = 0;
                }
            } else {
                ticks_to_wait = 0; // Pasó el tiempo sin que nadie toque nada
            }
        }

        // 4. Actualizar variables para el próximo ciclo
        time_to_blink -= note_duration;
        note_idx = (note_idx + 1) % num_notes;
    }
}