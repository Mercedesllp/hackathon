#include "menu.h"
#include "display.h"
#include "buttons.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok)
{
    button_event_t ev;

    // --- ANIMACIÓN DE ENTRADA ---
    if (display_ok) {
        const int anim_frames = 20;   // Cantidad de cuadros de la animación
        const int anim_delay = 25;    // Tiempo entre cuadros (en ms)
        
        for (int step = 0; step <= anim_frames; step++) {
            display_clear();

            // Interpolar posiciones desde el origen hasta el destino
            // 'S': De izquierda (x=0) a su posición final (x=49)
            int s_x  = 0 + (49 - 0) * step / anim_frames;
            
            // Primera 'T': De abajo (y=64) a (y=36)
            int t1_y = 64 + (36 - 64) * step / anim_frames;
            
            // 'A': De arriba (y=0) a (y=36)
            int a_y  = 0 + (36 - 0) * step / anim_frames;
            
            // 'R': De abajo (y=64) a (y=36)
            int r_y  = 64 + (36 - 64) * step / anim_frames;
            
            // Segunda 'T': De derecha (x=120) a (x=73)
            int t2_x = 120 + (73 - 120) * step / anim_frames;

            // Dibujar las letras separadas (sumando 6 píxeles de ancho por cada letra)
            display_text(s_x, 36, "S", false);
            display_text(55, t1_y, "T", false); // 49 + 6
            display_text(61, a_y,  "A", false); // 49 + 12
            display_text(67, r_y,  "R", false); // 49 + 18
            display_text(t2_x, 36, "T", false); // 49 + 24

            // Dibujar "SNAKE" al final para que quede por encima de la 'A' mientras cae
            display_text(49, 12, "SNAKE", false);
            
            display_flush();
            vTaskDelay(pdMS_TO_TICKS(anim_delay));
        }
    }
    // ----------------------------

    // Descarta eventos viejos (incluso botones que se hayan apretado por ansiedad durante la animación)
    while (xQueueReceive(events, &ev, 0) == pdTRUE) {}

    bool visible = true;
    while (1) {
        if (display_ok) {
            draw_start(visible);
        }

        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(BLINK_MS)) == pdTRUE) {
            if (ev.pressed) {
                return ESP_OK;
            }
        } else {
            visible = !visible;
        }
    }
}