#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_random.h"

#include "minigames.h"
#include "display.h"
#include "buttons.h"
#include "audio.h"
#include "sounds.h"

typedef struct {
    const char *question;
    const char *options[3];
    int correct;
} quiz_question_t;

static const quiz_question_t quiz[] = {
    {"Que fue primero?",      {"A) El huevo", "B) El aura", "C) El silencio"}, 0},
    {"Apodo para Francisco",  {"A) CORNUDO", "B) BORRACHO", "C) PANCHO"}, 1},
    {"Pelu...",               {"A) ..queria", "B) ..che", "C) ..sa"}, 1},
    {"6x7?",                  {"A) 42", "B) Cuarenta y dos", "C) Cuarentidos"}, 1},
    {"Atomos en el universo?",{"A) 500 millones", "B) muchos", "C) 10^80"}, 2},
    {"Mejor infusion?",       {"A) Mate", "B) Te", "C) Cafe"}, 0},
    {"Direccion de Mer?",     {"A) Av. Diaz Velez 2786", "B) Segurola 3321", "C) Pabe 0"}, 1},
    {"100000000 + 1?",        {"A) 100000001", "B) 10000001", "C) 10000000001"}, 0},
    {"121x45?",               {"A) 4885", "B) 5445", "C) 1000"}, 1},
    {"Mejor dia?",            {"A) Sabado", "B) Viernes", "C) Manana"}, 2},
    {"Amargo y...",           {"A) Dulce", "B) Agrio", "C) Retruco"}, 2},
};

#define NUM_QUESTIONS ((int)(sizeof(quiz) / sizeof(quiz[0])))

bool minigames_run_quiz(bool display_ok, bool audio_ok, minigame_abort_fn should_abort) {
    int current_q = esp_random() % NUM_QUESTIONS;
    QueueHandle_t q = buttons_event_queue();
    xQueueReset(q);

    while (1) {
        if (should_abort && should_abort()) return false;

        if (display_ok) {
            display_clear();
            display_text(0, 0, "¡REVISA EL PUZZLE!", true);
            display_text(0, 12, quiz[current_q].question, false);
            for (int j = 0; j < 3; j++) {
                display_text(0, 28 + j * 10, quiz[current_q].options[j], false);
            }
            display_text(0, 56, "K1=A  K2=B  K3=C", false);
            display_flush();
        }

        button_event_t ev;
        if (xQueueReceive(q, &ev, pdMS_TO_TICKS(50)) == pdTRUE && ev.pressed) {
            int chosen = -1;
            if (ev.id == BTN_KEY1) chosen = 0;
            else if (ev.id == BTN_KEY2) chosen = 1;
            else if (ev.id == BTN_KEY3) chosen = 2;

            if (chosen != -1) {
                bool ok = (chosen == quiz[current_q].correct);
                if (ok) {
                    if (audio_ok) sounds_play_tone(880, 150);
                } else {
                    if (audio_ok) sounds_play_tone(220, 300);
                }

                if (display_ok) {
                    display_clear();
                    display_text(0, 20, ok ? "¡CORRECTO! +1 Bloque" : "INCORRECTO :(", true);
                    display_flush();
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                return ok;
            }
        }
    }
}

bool minigames_run_math(bool display_ok, bool audio_ok, minigame_abort_fn should_abort) {
    int a = (int)(esp_random() % 21);
    int b = (int)(esp_random() % 21);
    int c = (int)(esp_random() % 21);

    int op1_type = (int)(esp_random() % 2);
    int op2_type = (int)(esp_random() % 2);

    int intermediate = (op1_type == 0) ? (a + b) : (a - b);
    int correct_answer = (op2_type == 0) ? (intermediate + c) : (intermediate - c);

    char expr_str[32];
    snprintf(expr_str, sizeof(expr_str), "%d %c %d %c %d = ?",
             a, op1_type == 0 ? '+' : '-',
             b, op2_type == 0 ? '+' : '-',
             c);

    int options[3];
    int correct_slot = (int)(esp_random() % 3);
    options[correct_slot] = correct_answer;

    int offset_vals[3] = {a, b > 0 ? b : 1, c > 0 ? c : 2};
    int decoy_offset = offset_vals[esp_random() % 3];
    if (decoy_offset == 0) decoy_offset = 3;

    int decoy_idx = 0;
    for (int i = 0; i < 3; i++) {
        if (i == correct_slot) continue;
        options[i] = (decoy_idx == 0) ? (correct_answer + decoy_offset) : (correct_answer - decoy_offset);
        decoy_idx++;
    }

    if (options[0] == options[1]) options[1] += 1;
    if (options[1] == options[2]) options[2] += 2;

    QueueHandle_t q = buttons_event_queue();
    xQueueReset(q);

    while (1) {
        if (should_abort && should_abort()) return false;

        if (display_ok) {
            display_clear();
            display_text(0, 0, "¡CALCULO RAPIDO!", true);
            display_text(0, 14, expr_str, false);

            char opt_line[24];
            snprintf(opt_line, sizeof(opt_line), "A) %d", options[0]);
            display_text(0, 28, opt_line, false);

            snprintf(opt_line, sizeof(opt_line), "B) %d", options[1]);
            display_text(0, 38, opt_line, false);

            snprintf(opt_line, sizeof(opt_line), "C) %d", options[2]);
            display_text(0, 48, opt_line, false);

            display_flush();
        }

        button_event_t ev;
        if (xQueueReceive(q, &ev, pdMS_TO_TICKS(50)) == pdTRUE && ev.pressed) {
            int chosen_opt = -1;
            if (ev.id == BTN_KEY1) chosen_opt = 0;
            else if (ev.id == BTN_KEY2) chosen_opt = 1;
            else if (ev.id == BTN_KEY3) chosen_opt = 2;

            if (chosen_opt != -1) {
                bool ok = (chosen_opt == correct_slot);
                if (ok) {
                    if (audio_ok) sounds_play_tone(880, 150);
                } else {
                    if (audio_ok) sounds_play_tone(220, 300);
                }

                if (display_ok) {
                    display_clear();
                    display_text(0, 20, ok ? "¡CORRECTO! +1 Bloque" : "INCORRECTO :(", true);
                    display_flush();
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
                return ok;
            }
        }
    }
}

bool minigames_run_fishing(bool display_ok, bool audio_ok, minigame_abort_fn should_abort) {
    int target_y = 16;
    int target_dir = 1;
    float player_y = 30.0f;
    float progress = 0.0f;

    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        if (should_abort && should_abort()) return false;

        target_y += target_dir;
        if (target_y <= 2 || target_y >= 34) {
            target_dir *= -1;
        }

        int mic_val = audio_mic_level();
        if (mic_val > 45) {
            player_y -= 1.5f;
        } else {
            player_y += 1.2f;
        }

        if (player_y < 0) player_y = 0;
        if (player_y > 44) player_y = 44;

        if (player_y >= target_y && player_y <= (target_y + 14)) {
            progress += 2.0f;
            if (progress >= 100.0f) {
                if (audio_ok) sounds_play_tone(880, 150);
                return true;
            }
        } else {
            progress -= 1.5f;
            if (progress < 0.0f) progress = 0.0f;
        }

        if (display_ok) {
            display_clear();
            display_text(4, 20, "¡SOPLA!", false);
            display_text(4, 32, "PESCA!", false);

            display_draw_rect(88, 10, 5, 48);
            int fill_h = (int)(progress * 48 / 100);
            display_fill_rect(89, 10 + (48 - fill_h), 3, fill_h, true);

            display_draw_rect(96, 10, 14, 48);
            display_fill_rect(98, 10 + target_y, 10, 14, true);
            display_fill_rect(95, 10 + (int)player_y, 16, 4, false);

            display_flush();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(33));
    }
}

bool minigames_run_random(bool display_ok, bool audio_ok, minigame_abort_fn should_abort) {
    int pick = esp_random() % 3;
    if (pick == 0) return minigames_run_quiz(display_ok, audio_ok, should_abort);
    if (pick == 1) return minigames_run_math(display_ok, audio_ok, should_abort);
    return minigames_run_fishing(display_ok, audio_ok, should_abort);
}