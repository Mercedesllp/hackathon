#include "menu.h"
#include "display.h"
#include "buttons.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern void wifi_audio_play_tone(int freq_hz, int ms);

#define BLINK_MS 500

static void draw_start(bool visible) {
    display_clear();
    display_text(49, 12, "SNAKE", false);
    if (visible) display_text(49, 36, "START", false);
    display_flush();
}

static void draw_mode_selection(int current_mode, bool visible) {
    display_clear();
    display_text(49, 4, "SNAKE", false); // Subimos el título un poquito
    
    // Opción 1 Jugador
    if (current_mode == 1) {
        if (visible) display_text(25, 28, "> 1 JUGADOR <", false);
        else display_text(37, 28, "1 JUGADOR", false);
    } else {
        display_text(37, 28, "1 JUGADOR", false);
    }

    // Opción 2 Jugadores
    if (current_mode == 2) {
        if (visible) display_text(19, 44, "> 2 JUGADORES <", false);
        else display_text(31, 44, "2 JUGADORES", false);
    } else {
        display_text(31, 44, "2 JUGADORES", false);
    }
    
    display_flush();
}

esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok, bool audio_ok, int *selected_mode) {
    button_event_t ev;

    // --- ANIMACIÓN DE ENTRADA (S-N-A-K-E) ---
    if (display_ok) {
        const int anim_frames = 20;
        const int anim_delay = 25;
        for (int step = 0; step <= anim_frames; step++) {
            display_clear();
            
            // S: De izquierda (x=0) a su posición final (x=49)
            int s_x  = 0 + (49 - 0) * step / anim_frames;
            // N: De abajo (y=64) a (y=12)
            int n_y  = 64 + (12 - 64) * step / anim_frames;
            // A: De arriba (y=0) a (y=12)
            int a_y  = 0 + (12 - 0) * step / anim_frames;
            // K: De abajo (y=64) a (y=12)
            int k_y  = 64 + (12 - 64) * step / anim_frames;
            // E: De derecha (x=120) a (x=73)
            int e_x  = 120 + (73 - 120) * step / anim_frames;

            display_text(s_x, 12, "S", false);
            display_text(55, n_y, "N", false);
            display_text(61, a_y, "A", false);
            display_text(67, k_y, "K", false);
            display_text(e_x, 12, "E", false);
            
            display_flush();
            vTaskDelay(pdMS_TO_TICKS(anim_delay));
        }
    }
    
    // Limpiamos los botones apretados por ansiedad
    while (xQueueReceive(events, &ev, 0) == pdTRUE) {}

    // Configuración de la música
    const int notes[] = {196, 220, 262, 220}; // Sol, La, Do, La (Octavas más graves)
    const int note_duration = 250; // El doble de lento (250ms por nota)
    int num_notes = 4;
    int note_idx = 0;
    
    int time_to_blink = 0; 
    bool visible = false;
    
    // Variables de estado del menú
    bool in_mode_selection = false;
    int current_mode = 1;

    while (1) {
        // 1. Manejo del parpadeo
        if (time_to_blink <= 0) {
            visible = !visible;
            if (display_ok) {
                if (!in_mode_selection) draw_start(visible);
                else draw_mode_selection(current_mode, visible);
            }
            time_to_blink = BLINK_MS;
        }

        // 2. Tocar la nota
        if (audio_ok) wifi_audio_play_tone(notes[note_idx], note_duration);

        TickType_t start_tick = xTaskGetTickCount();
        TickType_t ticks_to_wait = pdMS_TO_TICKS(note_duration);
        
        // 3. Esperar a que termine la nota mientras leemos botones
        while (ticks_to_wait > 0) {
            if (xQueueReceive(events, &ev, ticks_to_wait) == pdTRUE) {
                if (ev.pressed) {
                    if (!in_mode_selection) {
                        // Pasar de "START" a Selección de Modo
                        in_mode_selection = true;
                        visible = true;
                        time_to_blink = BLINK_MS;
                        if (audio_ok) wifi_audio_play_tone(880, 100);
                        if (display_ok) draw_mode_selection(current_mode, visible);
                    } else {
                        // Navegar por el submenú solo con el joystick (arriba/abajo)
                        if (ev.id == BTN_UP || ev.id == BTN_DOWN) {
                            current_mode = (current_mode == 1) ? 2 : 1; // Alterna entre 1 y 2
                            visible = true; 
                            time_to_blink = BLINK_MS;
                            if (audio_ok) wifi_audio_play_tone(440, 50);
                            if (display_ok) draw_mode_selection(current_mode, visible);
                        } 
                        // Confirmar modo SOLO con el BOTÓN 1
                        else if (ev.id == BTN_KEY1) {
                            if (audio_ok) wifi_audio_play_tone(1046, 400);
                            *selected_mode = current_mode;
                            return ESP_OK;
                        }
                    }
                    }
                }
                
                TickType_t elapsed = xTaskGetTickCount() - start_tick;
                if (elapsed < pdMS_TO_TICKS(note_duration)) {
                    ticks_to_wait = pdMS_TO_TICKS(note_duration) - elapsed;
                } else {
                    ticks_to_wait = 0;
                }
            } else {
                ticks_to_wait = 0; 
            }
        }

        // 4. Ciclar al siguiente frame de música
        time_to_blink -= note_duration;
        note_idx = (note_idx + 1) % num_notes;
    }
}

static void draw_pause_menu(int current_option) {
    display_clear();
    display_text(49, 12, "PAUSA", false);
    
    // Opción Continuar (0)
    if (current_option == 0) {
        display_text(25, 36, "> CONTINUAR <", false);
    } else {
        display_text(37, 36, "CONTINUAR", false);
    }

    // Opción Salir (1)
    if (current_option == 1) {
        display_text(37, 52, "> SALIR <", false);
    } else {
        display_text(49, 52, "SALIR", false);
    }
    
    display_flush();
}

pause_choice_t menu_pause(QueueHandle_t events, bool display_ok, bool audio_ok) {
    button_event_t ev;
    int current_option = 0; 
    
    // Limpiamos botones apretados por accidente antes de pausar
    while (xQueueReceive(events, &ev, 0) == pdTRUE) {}
    
    if (display_ok) draw_pause_menu(current_option);
    if (audio_ok) wifi_audio_play_tone(880, 100);

    while (1) {
        if (xQueueReceive(events, &ev, portMAX_DELAY) == pdTRUE) {
            if (ev.pressed) {
                // Moverse arriba/abajo solo con el joystick
                if (ev.id == BTN_UP || ev.id == BTN_DOWN) {
                    current_option = !current_option; // Alterna entre 0 y 1
                    if (audio_ok) wifi_audio_play_tone(440, 50);
                    if (display_ok) draw_pause_menu(current_option);
                } 
                // Seleccionar opción con el BOTÓN 1
                else if (ev.id == BTN_KEY1) {
                    if (audio_ok) wifi_audio_play_tone(1046, 200);
                    return (pause_choice_t)current_option;
                }
            }
        }
    }
}