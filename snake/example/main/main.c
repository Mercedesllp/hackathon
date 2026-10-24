#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "buttons.h"
#include "display.h"
#include "menu.h"
#include "board.h"
#include "sdkconfig.h"
#include "radio.h"

// 1 = esta placa arranca con la serpiente (placa 1), 0 = arranca sin ella (placa 2)
#define EMPIEZA_CON_SERPIENTE 1

#define WIFI_SSID   "kiwi-audio"
#define WIFI_PASS   "kiwikiwi"

static httpd_handle_t s_server = NULL;
static const char *TAG = "demo";

// -- CONSTANTES DEL JUEGO --
#define SQUARE_SIZE 4
#define WALL_SIZE 1
#define MAX_TAIL 200
#define GAME_SPEED_MS 200

// -- ESTADO DEL JUEGO --
typedef struct {
    int x, y;
    int dir_x, dir_y;
    int score;
    int tail_x[MAX_TAIL];
    int tail_y[MAX_TAIL];
    int apple_x, apple_y;

    bool game_over;
    bool snake_is_here;
    bool portals_open;
    int wall_anim_step;

    int max_grid_x;
    int max_grid_y;

    bool audio_ok;
    bool display_ok;

    int votos_arriba;
    int votos_abajo;
    int votos_izq;
    int votos_der;

    bool is_multiplayer;
} GameState;

static GameState game;

// --- INICIO SISTEMA DE AUDIO POR WIFI ---
static const char s_page[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>Snake Audio</title>"
    "<style>body{font-family:sans-serif;text-align:center;padding-top:15vh;background:#222;color:#fff}"
    "button{font-size:1.6em;padding:1em 2em;margin:.4em;border-radius:10px;background:#4CAF50;color:white;border:none}</style></head><body>"
    "<h1>Snake Audio</h1>"
    "<button id=go>Conectar Parlante</button>"
    "<p id=st>Toca el boton para escuchar el juego</p><script>"
    "let ctx,ws;"
    "go.onclick=()=>{ctx=ctx||new AudioContext();ctx.resume();connect()};"
    "function connect(){if(ws&&ws.readyState<2)return;"
    "ws=new WebSocket('ws://'+location.host+'/ws');"
    "ws.onopen=()=>{st.textContent='Conectado! Volve al juego.';};"
    "ws.onclose=()=>{st.textContent='Desconectado, reintentando...';setTimeout(connect,1000)};"
    "ws.onmessage=e=>{const[f,ms]=e.data.split(' ').map(Number);tone(f,ms/1000)}}"
    "function tone(f,d){const t=ctx.currentTime,o=ctx.createOscillator(),g=ctx.createGain();"
    "o.frequency.value=f;g.gain.setValueAtTime(0,t);g.gain.linearRampToValueAtTime(.3,t+.005);"
    "g.gain.setValueAtTime(.3,t+d-.005);g.gain.linearRampToValueAtTime(0,t+d);"
    "o.connect(g).connect(ctx.destination);o.start(t);o.stop(t+d)}"
    "</script></body></html>";

static esp_err_t page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, s_page, sizeof(s_page) - 1);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Celular conectado");
    }
    return ESP_OK;
}

static void broadcast_audio(void *arg)
{
    char text[24];
    uintptr_t packed = (uintptr_t)arg;
    int len = snprintf(text, sizeof(text), "%d %d",
                       (int)(packed >> 16), (int)(packed & 0xffff));

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = len,
    };

    int fds[CONFIG_LWIP_MAX_SOCKETS];
    size_t n = CONFIG_LWIP_MAX_SOCKETS;
    if (httpd_get_client_list(s_server, &n, fds) == ESP_OK) {
        for (size_t i = 0; i < n; i++) {
            if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_send_frame_async(s_server, fds[i], &frame);
            }
        }
    }
}

// Audio no bloqueante: manda el tono a los celulares conectados
void wifi_audio_play_tone(int freq_hz, int ms)
{
    if (s_server != NULL) {
        httpd_queue_work(s_server, broadcast_audio,
                         (void *)(uintptr_t)(((uintptr_t)freq_hz << 16) | (uintptr_t)(ms & 0xffff)));
    }
}

static bool wifi_audio_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&s_server, &cfg) == ESP_OK) {
        static const httpd_uri_t page = {
            .uri = "/", .method = HTTP_GET, .handler = page_handler,
        };
        static const httpd_uri_t ws = {
            .uri = "/ws", .method = HTTP_GET, .handler = ws_handler,
            .is_websocket = true,
        };
        httpd_register_uri_handler(s_server, &page);
        httpd_register_uri_handler(s_server, &ws);
        ESP_LOGI(TAG, "Conectate al Wi-Fi '%s' (clave '%s') y entra a http://192.168.4.1",
                 WIFI_SSID, WIFI_PASS);
        return true;
    }
    return false;
}
// --- FIN SISTEMA DE AUDIO POR WIFI ---

// -- FUNCIONES DE LÓGICA --

static void reset_game(void)
{
    game.x = WALL_SIZE + (game.max_grid_x / 2) * SQUARE_SIZE;
    game.y = WALL_SIZE + (game.max_grid_y / 2) * SQUARE_SIZE;
    game.dir_x = 1;
    game.dir_y = 0;
    game.score = 0;
    game.apple_x = WALL_SIZE + (esp_random() % game.max_grid_x) * SQUARE_SIZE;
    game.apple_y = WALL_SIZE + (esp_random() % game.max_grid_y) * SQUARE_SIZE;
    game.game_over = false;
    game.portals_open = false;
    // En 1 jugador la serpiente siempre está; en multijugador depende de la placa
    game.snake_is_here = !game.is_multiplayer || EMPIEZA_CON_SERPIENTE;
    game.wall_anim_step = 0;
}

static void process_inputs(QueueHandle_t events, bool *back_to_menu)
{
    button_event_t ev;
    game.votos_arriba = 0;
    game.votos_abajo = 0;
    game.votos_izq = 0;
    game.votos_der = 0;

    while (xQueueReceive(events, &ev, 0) == pdTRUE) {
        if (!ev.pressed) continue;

        if (game.game_over) {
            *back_to_menu = true;
            continue;
        }

        switch (ev.id) {
            // --- MOVIMIENTOS ---
            case BTN_UP:    game.votos_izq++;    break;
            case BTN_RIGHT: game.votos_arriba++; break;
            case BTN_DOWN:  game.votos_der++;    break;
            case BTN_LEFT:  game.votos_abajo++;  break;

            // --- BOTÓN DE PAUSA ---
            case BTN_KEY1:
                if (menu_pause(events, game.display_ok, game.audio_ok) == PAUSE_QUIT) {
                    *back_to_menu = true;
                }
                break;
            default: break;
        }
    }
}

static void resolve_new_direction(void)
{
    int max_votos = 0;
    int nuevo_dir_x = game.dir_x;
    int nuevo_dir_y = game.dir_y;

    if (game.votos_arriba > max_votos && game.dir_y == 0) { max_votos = game.votos_arriba; nuevo_dir_x = 0;  nuevo_dir_y = -1; }
    if (game.votos_abajo  > max_votos && game.dir_y == 0) { max_votos = game.votos_abajo;  nuevo_dir_x = 0;  nuevo_dir_y = 1;  }
    if (game.votos_izq    > max_votos && game.dir_x == 0) { max_votos = game.votos_izq;    nuevo_dir_x = -1; nuevo_dir_y = 0;  }
    if (game.votos_der    > max_votos && game.dir_x == 0) { max_votos = game.votos_der;    nuevo_dir_x = 1;  nuevo_dir_y = 0;  }

    if (max_votos > 0) {
        game.dir_x = nuevo_dir_x;
        game.dir_y = nuevo_dir_y;
    }
}

static void check_walls(void)
{
    // Dividir con negativos en C trunca hacia 0, así que -4/4 = -1 pero -1/4 = 0.
    // Como la serpiente se mueve de a SQUARE_SIZE, x=-3 -> (-3-1)/4 = -1: funciona.
    int gx = (game.x - WALL_SIZE) / SQUARE_SIZE;
    int gy = (game.y - WALL_SIZE) / SQUARE_SIZE;

    if (game.x < WALL_SIZE) gx = -1;
    if (game.y < WALL_SIZE) gy = -1;

    if (gx >= 0 && gx < game.max_grid_x && gy >= 0 && gy < game.max_grid_y) {
        return; // dentro del tablero
    }

    if (game.portals_open) {
        if (game.is_multiplayer) {
            game.snake_is_here = false; // Se va a la otra placa
            // TODO: ENVIAR DATOS (score, dir, posición) POR I2C
            if (game.audio_ok) wifi_audio_play_tone(440, 150);
        } else {
            // 1 JUGADOR: aparece por el borde opuesto
            if (gx < 0)                    game.x = WALL_SIZE + (game.max_grid_x - 1) * SQUARE_SIZE;
            else if (gx >= game.max_grid_x) game.x = WALL_SIZE;

            if (gy < 0)                    game.y = WALL_SIZE + (game.max_grid_y - 1) * SQUARE_SIZE;
            else if (gy >= game.max_grid_y) game.y = WALL_SIZE;

            if (game.audio_ok) wifi_audio_play_tone(660, 150);
        }
    } else {
        game.game_over = true;
        if (game.audio_ok) wifi_audio_play_tone(150, 400);
    }
}

static void check_apple_collision(void)
{
    if (game.x == game.apple_x && game.y == game.apple_y) {
        if (game.score < MAX_TAIL) game.score++;
        if (game.audio_ok) wifi_audio_play_tone(880, 50);
        game.apple_x = WALL_SIZE + (esp_random() % game.max_grid_x) * SQUARE_SIZE;
        game.apple_y = WALL_SIZE + (esp_random() % game.max_grid_y) * SQUARE_SIZE;
    }
}

static void update_tail_positions(int prev_x, int prev_y)
{
    for (int i = game.score - 1; i > 0; i--) {
        game.tail_x[i] = game.tail_x[i - 1];
        game.tail_y[i] = game.tail_y[i - 1];
    }
    if (game.score > 0) {
        game.tail_x[0] = prev_x;
        game.tail_y[0] = prev_y;
    }
}

static void check_self_collision(void)
{
    for (int i = 0; i < game.score; i++) {
        if (game.x == game.tail_x[i] && game.y == game.tail_y[i]) {
            game.game_over = true;
            if (game.audio_ok) wifi_audio_play_tone(150, 400);
            return;
        }
    }
}

// -- FUNCIONES DE RENDERIZADO --

static void render_walls(void)
{
    int w = BOARD_LCD_WIDTH;
    int h = BOARD_LCD_HEIGHT;
    int step = game.wall_anim_step;

    if (step > w / 2) step = w / 2;

    int horiz_w = (w / 2) - step;
    int vert_h = (h / 2) - (step / 2);

    if (horiz_w > 0) {
        // Techo
        display_fill_rect(0, 0, horiz_w, WALL_SIZE, true);
        display_fill_rect((w / 2) + step, 0, horiz_w, WALL_SIZE, true);
        // Piso
        display_fill_rect(0, h - WALL_SIZE, horiz_w, WALL_SIZE, true);
        display_fill_rect((w / 2) + step, h - WALL_SIZE, horiz_w, WALL_SIZE, true);
    }

    if (vert_h > 0) {
        // Pared izquierda
        display_fill_rect(0, 0, WALL_SIZE, vert_h, true);
        display_fill_rect(0, (h / 2) + (step / 2), WALL_SIZE, vert_h, true);
        // Pared derecha
        display_fill_rect(w - WALL_SIZE, 0, WALL_SIZE, vert_h, true);
        display_fill_rect(w - WALL_SIZE, (h / 2) + (step / 2), WALL_SIZE, vert_h, true);
    }
}

static void render_game_over(void)
{
    display_text(37, 20, "GAME OVER", false);
    char final_score[16];
    snprintf(final_score, sizeof(final_score), "Puntos: %d", game.score);
    display_text(37, 36, final_score, false);
}

static void render_snake_here(void)
{
    // Manzana
    display_fill_rect(game.apple_x, game.apple_y, SQUARE_SIZE, SQUARE_SIZE, true);
    display_fill_rect(game.apple_x + 1, game.apple_y + 1, 1, 1, false);

    // Cabeza y cola
    display_fill_rect(game.x, game.y, SQUARE_SIZE, SQUARE_SIZE, true);
    for (int i = 0; i < game.score; i++) {
        display_fill_rect(game.tail_x[i], game.tail_y[i], SQUARE_SIZE, SQUARE_SIZE, true);
    }
}

static void render_waiting(void)
{
    display_text(10, 28, "ESPERANDO A AMIGO", false);
}

// -- BUCLE PRINCIPAL --

void app_main(void)
{
    ESP_LOGI(TAG, "Hackathon board demo starting");

    game.display_ok = display_init() == ESP_OK;
    ESP_ERROR_CHECK(buttons_init());

    game.audio_ok = wifi_audio_init();

    bool radio_ok = radio_init() == ESP_OK;
    (void)radio_ok; // TODO: usar cuando se implemente la comunicación entre placas

    if (game.audio_ok) {
        wifi_audio_play_tone(523, 120);
    }

    QueueHandle_t events = buttons_event_queue();

    game.max_grid_x = (BOARD_LCD_WIDTH - 2 * WALL_SIZE) / SQUARE_SIZE;
    game.max_grid_y = (BOARD_LCD_HEIGHT - 2 * WALL_SIZE) / SQUARE_SIZE;

    // BUCLE EXTERNO: Menú -> Juego -> Menú
    while (1) {
        int selected_mode = 1;
        ESP_ERROR_CHECK(menu_wait_for_start(events, game.display_ok, game.audio_ok, &selected_mode));

        game.is_multiplayer = (selected_mode == 2);
        reset_game();

        bool back_to_menu = false;
        TickType_t last_wake_time = xTaskGetTickCount();

        // BUCLE INTERNO: partida en curso
        while (!back_to_menu) {
            process_inputs(events, &back_to_menu);

            if (!game.game_over) {
                // Los portales se abren cuando el score termina en 2 (2, 5, 8, 11...)
                game.portals_open = ((game.score % 3) == 2);

                if (game.portals_open) {
                    if (game.wall_anim_step < 128) game.wall_anim_step += 16;
                } else {
                    if (game.wall_anim_step > 0) game.wall_anim_step -= 16;
                }

                if (game.snake_is_here) {
                    resolve_new_direction();

                    int prev_x = game.x;
                    int prev_y = game.y;

                    game.x += game.dir_x * SQUARE_SIZE;
                    game.y += game.dir_y * SQUARE_SIZE;

                    check_walls();

                    if (!game.game_over && game.snake_is_here) {
                        check_apple_collision();
                        update_tail_positions(prev_x, prev_y);
                        check_self_collision();
                    }
                } else {
                    // TODO: LEER DATOS DE LA OTRA PLACA ACÁ

                    // HACK TEMPORAL PARA TESTING: cualquier dirección "trae" la serpiente de vuelta
                    if (game.votos_arriba > 0 || game.votos_abajo > 0 ||
                        game.votos_izq > 0 || game.votos_der > 0) {

                        game.snake_is_here = true;

                        int gx = (game.x - WALL_SIZE) / SQUARE_SIZE;
                        int gy = (game.y - WALL_SIZE) / SQUARE_SIZE;
                        if (game.x < WALL_SIZE) gx = -1;
                        if (game.y < WALL_SIZE) gy = -1;

                        if (gx < 0)                        game.x = WALL_SIZE + (game.max_grid_x - 1) * SQUARE_SIZE;
                        else if (gx >= game.max_grid_x)    game.x = WALL_SIZE;

                        if (gy < 0)                        game.y = WALL_SIZE + (game.max_grid_y - 1) * SQUARE_SIZE;
                        else if (gy >= game.max_grid_y)    game.y = WALL_SIZE;

                        if (game.audio_ok) wifi_audio_play_tone(660, 150);
                    }
                }
            }

            // Renderizado
            if (game.display_ok) {
                display_clear();
                render_walls();

                if (game.game_over) {
                    render_game_over();
                } else if (game.snake_is_here) {
                    render_snake_here();
                } else {
                    render_waiting();
                }

                display_flush();
            }

            vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GAME_SPEED_MS));
        }
    }
}