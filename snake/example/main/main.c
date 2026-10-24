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
#include "snake.h"
#include "radio.h"
#include "walls.h"

#define WIFI_SSID   "kiwi-audio"
#define WIFI_PASS   "kiwikiwi"

static httpd_handle_t s_server = NULL;
static const char *TAG = "demo";

// -- CONSTANTES DEL JUEGO --
#define SQUARE_SIZE 4
#define WALL_SIZE 1
#define MAX_TAIL 200
#define GAME_SPEED_MS 200

bool empieza_con_serpiente = true;

// -- ESTADO DEL JUEGO --
typedef struct {
    int x, y;
    int dir_x, dir_y;
    int score;        // largo total de la serpiente (y los puntos)
    int cola_visible; // cuántos pedazos de cola están de verdad en ESTA pantalla
    int tail_x[MAX_TAIL];
    int tail_y[MAX_TAIL];
    int apple_x, apple_y;

    bool game_over;
    bool snake_is_here;
    bool portals_open;
    int wall_anim_step;

    // Estado para pausar el juego y mostrar el menú
    bool in_wall_menu;

    // Variables de la "puertita" de entrada
    int entry_steps_left;
    int entry_door_x;
    int entry_door_y;
    int entry_dir_x;
    int entry_dir_y;

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

// -- JUGADOR QUE ESPERA: apunta a ciegas y le tira piezas a la otra placa --
#define PIECE_GRANT_MS 5000

typedef struct {
    int cursor_x, cursor_y; // dónde cae el ancla de la pieza, en celdas
    shape_id_t piece;       // la que se va a tirar
    shape_id_t next;        // la que viene después (se muestra arriba a la derecha)
    int rotation;           // 0..3
    TickType_t last_grant;  // para el regalo de piezas (stand-in del quiz)
} IdleState;

static IdleState idle;

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

void wifi_audio_play_tone(int freq_hz, int ms)
{
    if (s_server != NULL) {
        httpd_queue_work(s_server, broadcast_audio,
                         (void *)(uintptr_t)(((uintptr_t)freq_hz << 16) | (uintptr_t)(ms & 0xffff)));
    }
}

static bool wifi_audio_init(void)
{
    esp_log_level_set("wifi", ESP_LOG_ERROR); 

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
    game.cola_visible = 0;
    game.apple_x = WALL_SIZE + (esp_random() % game.max_grid_x) * SQUARE_SIZE;
    game.apple_y = WALL_SIZE + (esp_random() % game.max_grid_y) * SQUARE_SIZE;
    
    game.game_over = false;
    game.portals_open = false;
    game.wall_anim_step = 0;
    
    game.in_wall_menu = false; 
    game.entry_steps_left = 0;

    game.snake_is_here = !game.is_multiplayer || empieza_con_serpiente;
    walls_reset();

    idle.cursor_x = game.max_grid_x / 2;
    idle.cursor_y = game.max_grid_y / 2;
    idle.piece = walls_bag_next();
    idle.next = walls_bag_next();
    idle.rotation = 0;
    idle.last_grant = xTaskGetTickCount();
}

static void idle_drop_piece(void)
{
    if (!walls_take_piece()) {
        if (game.audio_ok) wifi_audio_play_tone(200, 80); 
        return;
    }
    message_t m = {
        .type = MSG_WALL,
        .x = idle.cursor_x,
        .y = idle.cursor_y,
        .shape = idle.piece,
        .rotation = idle.rotation,
    };
    radio_send(&m);
    if (game.audio_ok) wifi_audio_play_tone(990, 80);

    idle.piece = idle.next;
    idle.next = walls_bag_next();
    idle.rotation = 0;
}

static void idle_handle_button(button_id_t id)
{
    switch (id) {
        case BTN_UP:    idle.cursor_x--; break;
        case BTN_RIGHT: idle.cursor_y--; break;
        case BTN_DOWN:  idle.cursor_x++; break;
        case BTN_LEFT:  idle.cursor_y++; break;
        case BTN_KEY2:  idle.rotation = (idle.rotation + 1) % NUM_ROTATIONS; break;
        case BTN_KEY3:  idle_drop_piece(); break;
        default: break;
    }
    if (idle.cursor_x < 0) idle.cursor_x = 0;
    if (idle.cursor_x >= game.max_grid_x) idle.cursor_x = game.max_grid_x - 1;
    if (idle.cursor_y < 0) idle.cursor_y = 0;
    if (idle.cursor_y >= game.max_grid_y) idle.cursor_y = game.max_grid_y - 1;
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

        if (game.is_multiplayer && !game.snake_is_here && ev.id != BTN_KEY1) {
            idle_handle_button(ev.id);
            continue;
        }

        switch (ev.id) {
            case BTN_UP:    game.votos_izq++;    break;
            case BTN_RIGHT: game.votos_arriba++; break;
            case BTN_DOWN:  game.votos_der++;    break;
            case BTN_LEFT:  game.votos_abajo++;  break;

            case BTN_KEY1:
                if (menu_pause(events, game.display_ok, game.audio_ok) == PAUSE_QUIT) {
                    *back_to_menu = true;
                }
                break;
            
            case BTN_KEY4:
                if (!game.is_multiplayer) {
                    walls_add(esp_random() % NUM_SHAPES, esp_random() % NUM_ROTATIONS,
                              esp_random() % game.max_grid_x, esp_random() % game.max_grid_y);
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
    int gx = (game.x - WALL_SIZE) / SQUARE_SIZE;
    int gy = (game.y - WALL_SIZE) / SQUARE_SIZE;

    if (game.x < WALL_SIZE) gx = -1;
    if (game.y < WALL_SIZE) gy = -1;

    if (gx >= 0 && gx < game.max_grid_x && gy >= 0 && gy < game.max_grid_y) {
        return; 
    }

    int w = BOARD_LCD_WIDTH;
    int h = BOARD_LCD_HEIGHT;
    int step = game.wall_anim_step;
    
    if (step > w / 2) step = w / 2;

    int horiz_w = (w / 2) - step;
    int vert_h = (h / 2) - (step / 2);

    bool choca_pared = false;

    if (gx < 0 || gx >= game.max_grid_x) {
        if (game.y < vert_h || (game.y + SQUARE_SIZE) > (h / 2) + (step / 2)) {
            choca_pared = true;
        }
    } else if (gy < 0 || gy >= game.max_grid_y) {
        if (game.x < horiz_w || (game.x + SQUARE_SIZE) > (w / 2) + step) {
            choca_pared = true;
        }
    }
    
    if (game.entry_steps_left > 0) {
        choca_pared = false;
    }

    if (!choca_pared) {
        if (game.is_multiplayer) {
            game.snake_is_here = false; 
            
            message_t mensaje = {
                .type = MSG_SNAKE,
                .x = gx,
                .y = gy,
                .score = game.score,
            };
            radio_send(&mensaje);

            if (game.audio_ok) wifi_audio_play_tone(440, 150);
        } else {
            game.in_wall_menu = true;
            
            if (gx < 0)                    game.x = WALL_SIZE + (game.max_grid_x - 1) * SQUARE_SIZE;
            else if (gx >= game.max_grid_x) game.x = WALL_SIZE;

            if (gy < 0)                    game.y = WALL_SIZE + (game.max_grid_y - 1) * SQUARE_SIZE;
            else if (gy >= game.max_grid_y) game.y = WALL_SIZE;

            if (game.audio_ok) wifi_audio_play_tone(440, 150); 
        }
    } else {
        game.game_over = true;
        if (game.audio_ok) wifi_audio_play_tone(150, 400);
    }
}

static void place_apple(void)
{
    int gx, gy;
    int intentos = 0;
    do {
        gx = esp_random() % game.max_grid_x;
        gy = esp_random() % game.max_grid_y;
    } while (walls_hit(gx, gy) && ++intentos < 100);
    game.apple_x = WALL_SIZE + gx * SQUARE_SIZE;
    game.apple_y = WALL_SIZE + gy * SQUARE_SIZE;
}

static void check_apple_collision(void)
{
    if (game.x == game.apple_x && game.y == game.apple_y) {
        if (game.score < MAX_TAIL) game.score++;
        if (game.audio_ok) wifi_audio_play_tone(880, 50);
        place_apple();
    }
}

static void receive_messages(void)
{
    message_t m;
    while (radio_receive(&m)) {
        switch (m.type) {
            case MSG_SNAKE:
                if (!game.snake_is_here &&
                    snake_arrive(&m, &game.x, &game.y, &game.dir_x, &game.dir_y)) {
                    game.snake_is_here = true;
                    game.score = m.score;
                    game.cola_visible = 0;

                    // Configuramos la puertita de entrada
                    game.entry_steps_left = game.score + 1; 
                    game.entry_door_x = game.x;
                    game.entry_door_y = game.y;
                    game.entry_dir_x = game.dir_x;
                    game.entry_dir_y = game.dir_y;
                    game.wall_anim_step = 0;

                    if (game.audio_ok) wifi_audio_play_tone(660, 150);
                }
                break;

            case MSG_WALL:
                if (m.shape < NUM_SHAPES) {
                    walls_add(m.shape, m.rotation, m.x, m.y);
                    if (game.audio_ok) wifi_audio_play_tone(330, 100);
                }
                break;

            default:
                break;
        }
    }
}

static void update_tail_positions(int prev_x, int prev_y)
{
    if (game.cola_visible < game.score) game.cola_visible++;

    for (int i = game.cola_visible - 1; i > 0; i--) {
        game.tail_x[i] = game.tail_x[i - 1];
        game.tail_y[i] = game.tail_y[i - 1];
    }
    if (game.cola_visible > 0) {
        game.tail_x[0] = prev_x;
        game.tail_y[0] = prev_y;
    }
}

static void check_self_collision(void)
{
    for (int i = 0; i < game.cola_visible; i++) {
        if (game.x == game.tail_x[i] && game.y == game.tail_y[i]) {
            game.game_over = true;
            if (game.audio_ok) wifi_audio_play_tone(150, 400);
            return;
        }
    }
}

static bool snake_on_cell(int gx, int gy)
{
    if (!game.snake_is_here) {
        return false;
    }
    int px = WALL_SIZE + gx * SQUARE_SIZE;
    int py = WALL_SIZE + gy * SQUARE_SIZE;
    if (game.x == px && game.y == py) {
        return true;
    }
    for (int i = 0; i < game.cola_visible; i++) {
        if (game.tail_x[i] == px && game.tail_y[i] == py) {
            return true;
        }
    }
    return false;
}

static void check_wall_collision(void)
{
    int gx = (game.x - WALL_SIZE) / SQUARE_SIZE;
    int gy = (game.y - WALL_SIZE) / SQUARE_SIZE;
    if (walls_hit(gx, gy)) {
        game.game_over = true;
        if (game.audio_ok) wifi_audio_play_tone(150, 400);
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
        display_fill_rect(0, 0, horiz_w, WALL_SIZE, true);
        display_fill_rect((w / 2) + step, 0, horiz_w, WALL_SIZE, true);
        display_fill_rect(0, h - WALL_SIZE, horiz_w, WALL_SIZE, true);
        display_fill_rect((w / 2) + step, h - WALL_SIZE, horiz_w, WALL_SIZE, true);
    }

    if (vert_h > 0) {
        display_fill_rect(0, 0, WALL_SIZE, vert_h, true);
        display_fill_rect(0, (h / 2) + (step / 2), WALL_SIZE, vert_h, true);
        display_fill_rect(w - WALL_SIZE, 0, WALL_SIZE, vert_h, true);
        display_fill_rect(w - WALL_SIZE, (h / 2) + (step / 2), WALL_SIZE, vert_h, true);
    }
    
    if (game.entry_steps_left > 0) {
        int hole_x = game.entry_door_x;
        int hole_y = game.entry_door_y;
        int hole_w = SQUARE_SIZE;
        int hole_h = SQUARE_SIZE;

        if (game.entry_dir_x != 0) { 
            hole_w = WALL_SIZE;
            if (game.entry_dir_x == 1) hole_x = 0; 
            else hole_x = w - WALL_SIZE;           
        } else { 
            hole_h = WALL_SIZE;
            if (game.entry_dir_y == 1) hole_y = 0; 
            else hole_y = h - WALL_SIZE;           
        }
        
        display_fill_rect(hole_x, hole_y, hole_w, hole_h, false); 
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
    display_fill_rect(game.apple_x, game.apple_y, SQUARE_SIZE, SQUARE_SIZE, true);
    display_fill_rect(game.apple_x + 1, game.apple_y + 1, 1, 1, false);

    display_fill_rect(game.x, game.y, SQUARE_SIZE, SQUARE_SIZE, true);
    for (int i = 0; i < game.cola_visible; i++) {
        display_fill_rect(game.tail_x[i], game.tail_y[i], SQUARE_SIZE, SQUARE_SIZE, true);
    }
}

static void render_idle(void)
{
    int8_t cells[CELLS_PER_SHAPE][2];

    char hud[16];
    snprintf(hud, sizeof(hud), "Piezas:%d", walls_pieces());
    display_text(3, 3, hud, false);

    walls_shape_cells(idle.next, 0, cells);
    for (int i = 0; i < CELLS_PER_SHAPE; i++) {
        display_fill_rect(118 + cells[i][0] * 2, 5 + cells[i][1] * 2, 2, 2, true);
    }

    bool can_drop = walls_pieces() > 0;
    walls_shape_cells(idle.piece, idle.rotation, cells);
    for (int i = 0; i < CELLS_PER_SHAPE; i++) {
        int gx = idle.cursor_x + cells[i][0];
        int gy = idle.cursor_y + cells[i][1];
        if (gx < 0 || gx >= game.max_grid_x || gy < 0 || gy >= game.max_grid_y) {
            continue;
        }
        int px = WALL_SIZE + gx * SQUARE_SIZE;
        int py = WALL_SIZE + gy * SQUARE_SIZE;
        if (can_drop) {
            display_fill_rect(px, py, SQUARE_SIZE, SQUARE_SIZE, true);
        } else {
            display_draw_rect(px, py, SQUARE_SIZE, SQUARE_SIZE);
        }
    }
}

// -- BUCLE PRINCIPAL --

void app_main(void)
{
    ESP_LOGI(TAG, "Hackathon board demo starting");

    game.display_ok = display_init() == ESP_OK;
    ESP_ERROR_CHECK(buttons_init());

    game.audio_ok = wifi_audio_init();

    bool radio_ok = radio_init() == ESP_OK;
    if (!radio_ok) ESP_LOGE(TAG, "Fallo al iniciar la radio");

    if (game.audio_ok) {
        wifi_audio_play_tone(523, 120);
    }

    QueueHandle_t events = buttons_event_queue();

    game.max_grid_x = (BOARD_LCD_WIDTH - 2 * WALL_SIZE) / SQUARE_SIZE;
    game.max_grid_y = (BOARD_LCD_HEIGHT - 2 * WALL_SIZE) / SQUARE_SIZE;

    while (1) {
        int selected_mode = 1;
        ESP_ERROR_CHECK(menu_wait_for_start(events, game.display_ok, game.audio_ok, &selected_mode));

        game.is_multiplayer = (selected_mode == 2);
        bool back_to_menu = false;

        // --- SALA DE ESPERA MULTIJUGADOR ---
        if (game.is_multiplayer) {
            bool amigo_encontrado = false;
            
            if (game.display_ok) {
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

            while (!amigo_encontrado && !back_to_menu) {
                
                if (xTaskGetTickCount() - ultimo_aviso > pdMS_TO_TICKS(300)) {
                    message_t msg_ready = { .type = MSG_READY, .score = mi_ticket };
                    radio_send(&msg_ready);
                    ultimo_aviso = xTaskGetTickCount();
                }

                message_t msg_in;
                if (radio_receive(&msg_in)) {
                    if (msg_in.type == MSG_READY) {
                        if (mi_ticket == msg_in.score) {
                            mi_ticket = esp_random() % 10000;
                            continue;
                        }

                        amigo_encontrado = true;
                        empieza_con_serpiente = (mi_ticket > msg_in.score);
                        
                        if (game.audio_ok) wifi_audio_play_tone(1046, 200);
                        
                        for (int i=0; i<3; i++) {
                            message_t msg_ready_confirm = { .type = MSG_READY, .score = mi_ticket };
                            radio_send(&msg_ready_confirm);
                            vTaskDelay(pdMS_TO_TICKS(50));
                        }
                    }
                }

                button_event_t ev;
                if (xQueueReceive(events, &ev, pdMS_TO_TICKS(50)) == pdTRUE) {
                    if (ev.pressed && ev.id == BTN_KEY1) {
                        back_to_menu = true;
                    }
                }
            }

            if (back_to_menu) continue; 
        }
        // --- FIN SALA DE ESPERA ---

        reset_game();
        TickType_t last_wake_time = xTaskGetTickCount();

        // BUCLE INTERNO: partida en curso
        while (!back_to_menu) {
            process_inputs(events, &back_to_menu);

            // --- ESTADO: MENÚ DE PONER PARED (Testing 1 Jugador) ---
            if (game.in_wall_menu) {
                int nueva_pared_x, nueva_pared_y;
                bool nueva_pared_vert;

                menu_place_wall(events, game.display_ok, game.audio_ok, 
                                &nueva_pared_x, &nueva_pared_y, &nueva_pared_vert);
                
                game.in_wall_menu = false;
                
                game.entry_steps_left = game.score + 1; 
                game.entry_door_x = game.x;
                game.entry_door_y = game.y;
                game.entry_dir_x = game.dir_x;
                game.entry_dir_y = game.dir_y;
                
                game.wall_anim_step = 0;
                if (game.audio_ok) wifi_audio_play_tone(660, 150);
                
                last_wake_time = xTaskGetTickCount();
                continue; 
            }

            // --- LÓGICA NORMAL ---
            if (!game.game_over) {
                
                receive_messages();
                walls_update(snake_on_cell);

                if (game.entry_steps_left > 0) {
                    game.portals_open = false;
                } else {
                    game.portals_open = ((game.score % 3) == 2);
                }

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
                    
                    if (game.entry_steps_left > 0) {
                        game.entry_steps_left--;
                    }

                    check_walls();

                    if (!game.game_over && game.snake_is_here) {
                        check_wall_collision();
                    }
                    if (!game.game_over && game.snake_is_here) {
                        check_apple_collision();
                        update_tail_positions(prev_x, prev_y);
                        check_self_collision();
                    }
                } else {
                    if (xTaskGetTickCount() - idle.last_grant >= pdMS_TO_TICKS(PIECE_GRANT_MS)) {
                        walls_grant_piece();
                        idle.last_grant = xTaskGetTickCount();
                    }
                }
            }

            // Renderizado
            if (game.display_ok && !game.in_wall_menu) {
                display_clear();
                render_walls();

                if (game.game_over) {
                    render_game_over();
                } else if (game.snake_is_here) {
                    walls_render();
                    render_snake_here();
                } else {
                    render_idle();
                }

                display_flush();
            }

            vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(GAME_SPEED_MS));
        }
    }
}