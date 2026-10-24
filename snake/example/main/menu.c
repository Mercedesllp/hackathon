#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_random.h"

#include "menu.h"
#include "display.h"
#include "buttons.h"
#include "board.h"
#include "radio.h"
#include "snake.h"
#include "sounds.h"

#define BLINK_MS 500

static void draw_start(bool visible) {
    display_clear();
    display_text(44, 12, "Esneik", false);
    if (visible) display_text(49, 36, "START", false);
    display_flush();
}

static void draw_mode_selection(int current_mode, bool visible) {
    display_clear();
    display_text(49, 4, "SNAKE", false);
    
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
    // --- ANIMACIÓN DE ENTRADA (E-s-n-e-i-k) ---
    if (display_ok) {
        const int anim_frames = 20;
        const int anim_delay = 25;
        for (int step = 0; step <= anim_frames; step++) {
            display_clear();
            
            // Posiciones finales centradas (ancho total de 6 letras = ~36px)
            int e1_x = 0   + (44 - 0)   * step / anim_frames; // Entra desde la izquierda
            int s_y  = 64  + (12 - 64)  * step / anim_frames; // Entra desde abajo
            int n_y  = 0   + (12 - 0)   * step / anim_frames; // Entra desde arriba
            int e2_y = 64  + (12 - 64)  * step / anim_frames; // Entra desde abajo
            int i_y  = 0   + (12 - 0)   * step / anim_frames; // Entra desde arriba
            int k_x  = 128 + (74 - 128) * step / anim_frames; // Entra desde la derecha

            display_text(e1_x, 12, "E", false);
            display_text(50,   s_y,  "s", false);
            display_text(56,   n_y,  "n", false);
            display_text(62,   e2_y, "e", false);
            display_text(68,   i_y,  "i", false);
            display_text(k_x,  12,   "k", false);
            
            display_flush();
            vTaskDelay(pdMS_TO_TICKS(anim_delay));
        }
    }
    
    while (xQueueReceive(events, &ev, 0) == pdTRUE) {}

    const int notes[] = {196, 220, 262, 220}; 
    const int note_duration = 250; 
    int num_notes = 4;
    int note_idx = 0;
    
    int time_to_blink = 0; 
    bool visible = false;
    bool in_mode_selection = false;
    int current_mode = 1;

    while (1) {
        if (time_to_blink <= 0) {
            visible = !visible;
            if (display_ok) {
                if (!in_mode_selection) draw_start(visible);
                else draw_mode_selection(current_mode, visible);
            }
            time_to_blink = BLINK_MS;
        }

        if (audio_ok) wifi_audio_play_tone(notes[note_idx], note_duration);

        TickType_t start_tick = xTaskGetTickCount();
        TickType_t ticks_to_wait = pdMS_TO_TICKS(note_duration);
        
        while (ticks_to_wait > 0) {
            if (xQueueReceive(events, &ev, ticks_to_wait) == pdTRUE) {
                if (ev.pressed) {
                    if (!in_mode_selection) {
                        in_mode_selection = true;
                        visible = true;
                        time_to_blink = BLINK_MS;
                        if (audio_ok) wifi_audio_play_tone(880, 100);
                        if (display_ok) draw_mode_selection(current_mode, visible);
                    } else {
                        if (ev.id == BTN_UP || ev.id == BTN_DOWN) {
                            current_mode = (current_mode == 1) ? 2 : 1;
                            visible = true; 
                            time_to_blink = BLINK_MS;
                            if (audio_ok) wifi_audio_play_tone(440, 50);
                            if (display_ok) draw_mode_selection(current_mode, visible);
                        } else if (ev.id == BTN_KEY1) {
                            if (audio_ok) wifi_audio_play_tone(1046, 400);
                            *selected_mode = current_mode;
                            return ESP_OK;
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

        time_to_blink -= note_duration;
        note_idx = (note_idx + 1) % num_notes;
    }
}

static void draw_pause_menu(int current_option) {
    display_clear();
    display_text(49, 12, "PAUSA", false);
    
    if (current_option == 0) {
        display_text(25, 36, "> CONTINUAR <", false);
    } else {
        display_text(37, 36, "CONTINUAR", false);
    }

    if (current_option == 1) {
        display_text(37, 52, "> SALIR <", false);
    } else {
        display_text(49, 52, "SALIR", false);
    }
    
    display_flush();
}

int menu_pause(QueueHandle_t events, bool display_ok, bool audio_ok) {
    button_event_t ev;
    int current_option = 0; 
    
    while (xQueueReceive(events, &ev, 0) == pdTRUE) {}
    
    if (display_ok) draw_pause_menu(current_option);
    if (audio_ok) wifi_audio_play_tone(880, 100);

    while (1) {
        if (xQueueReceive(events, &ev, portMAX_DELAY) == pdTRUE) {
            if (ev.pressed) {
                if (ev.id == BTN_UP || ev.id == BTN_DOWN) {
                    current_option = !current_option;
                    if (audio_ok) wifi_audio_play_tone(440, 50);
                    if (display_ok) draw_pause_menu(current_option);
                } else if (ev.id == BTN_KEY1) {
                    if (audio_ok) wifi_audio_play_tone(1046, 200);
                    return current_option;
                }
            }
        }
    }
}

bool menu_wait_for_friend(QueueHandle_t events, bool display_ok, bool audio_ok, bool *starts_with_snake) {
    if (display_ok) {
        display_clear();
        display_text(10, 28, "BUSCANDO AMIGO...", false);
        display_text(10, 44, "(Btn 1 para salir)", false);
        display_flush();
    }

    message_t basura;
    while (radio_receive(&basura)) {}
    button_event_t ev_basura;
    while (xQueueReceive(events, &ev_basura, 0)) {}

    TickType_t ultimo_aviso = 0;
    int mi_ticket = esp_random() % 10000;

    while (1) {
        if (xTaskGetTickCount() - ultimo_aviso > pdMS_TO_TICKS(300)) {
            message_t msg_ready = { .type = MSG_READY, .score = mi_ticket };
            radio_send(&msg_ready);
            ultimo_aviso = xTaskGetTickCount();
        }

        message_t msg_in;
        if (radio_receive(&msg_in) && msg_in.type == MSG_READY) {
            if (mi_ticket == msg_in.score) {
                mi_ticket = esp_random() % 10000;
                continue;
            }

            *starts_with_snake = (mi_ticket > msg_in.score);
            if (audio_ok) wifi_audio_play_tone(1046, 200);

            for (int i = 0; i < 3; i++) {
                message_t msg_ready_confirm = { .type = MSG_READY, .score = mi_ticket };
                radio_send(&msg_ready_confirm);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            return true;
        }

        button_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (ev.pressed && ev.id == BTN_KEY1) {
                return false;
            }
        }
    }
}