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
    int score;        
    int apples_eaten_here; // <--- Contador independiente para la placa actual
    int cola_visible; 
    int tail_x[MAX_TAIL];
    int tail_y[MAX_TAIL];
    int apple_x, apple_y;

    bool game_over;
    bool game_over_notified;
    
    bool snake_is_here;
    bool portals_open;
    int wall_anim_step;

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

// -- JUGADOR QUE ESPERA --
#define PIECE_GRANT_MS 5000
#define MAX_ADV_WALLS 50 

typedef struct {
    int cursor_x, cursor_y; 
    shape_id_t piece;       
    shape_id_t next;        
    int rotation;           
    TickType_t last_grant;  
    
    struct {
        int x, y;
        shape_id_t shape;
        int rotation;
    } adv_walls[MAX_ADV_WALLS];
    int adv_walls_count;
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

static esp_err_t page_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, s_page, sizeof(s_page) - 1);
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) ESP_LOGI(TAG, "Celular conectado");
    return ESP_OK;
}

static void broadcast_audio(void *arg) {
    char text[24];
    uintptr_t packed = (uintptr_t)arg;
    int len = snprintf(text, sizeof(text), "%d %d", (int)(packed >> 16), (int)(packed & 0xffff));

    httpd_ws_frame_t frame = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)text, .len = len };
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

void wifi_audio_play_tone(int freq_hz, int ms) {
    if (s_server != NULL) {
        httpd_queue_work(s_server, broadcast_audio, (void *)(uintptr_t)(((uintptr_t)freq_hz << 16) | (uintptr_t)(ms & 0xffff)));
    }
}

static bool wifi_audio_init(void) {
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
    wifi_config_t ap_cfg = { .ap = { .ssid = WIFI_SSID, .ssid_len = strlen(WIFI_SSID), .password = WIFI_PASS, .max_connection = 4, .authmode = WIFI_AUTH_WPA2_PSK } };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&s_server, &cfg) == ESP_OK) {
        static const httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = page_handler };
        static const httpd_uri_t ws = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true };
        httpd_register_uri_handler(s_server, &page);
        httpd_register_uri_handler(s_server, &ws);
        ESP_LOGI(TAG, "Conectate al Wi-Fi '%s'", WIFI_SSID);
        return true;
    }
    return false;
}
// --- FIN SISTEMA DE AUDIO ---

static void reset_game(void) {
    game.x = WALL_SIZE + (game.max_grid_x / 2) * SQUARE_SIZE;
    game.y = WALL_SIZE + (game.max_grid_y / 2) * SQUARE_SIZE;
    game.dir_x = 1;
    game.dir_y = 0;
    
    game.score = 0;
    game.apples_eaten_here = 0; // Se resetea al empezar
    
    game.cola_visible = 0;
    game.apple_x = WALL_SIZE + (esp_random() % game.max_grid_x) * SQUARE_SIZE;
    game.apple_y = WALL_SIZE + (esp_random() % game.max_grid_y) * SQUARE_SIZE;
    
    game.game_over = false;
    game.game_over_notified = false; 
    
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
    idle.adv_walls_count = 0; 
}

static void idle_drop_piece(void) {
    if (!walls_take_piece()) {
        if (game.audio_ok) wifi_audio_play_tone(200, 80); 
        return;
    }
    
    if (idle.adv_walls_count < MAX_ADV_WALLS) {
        idle.adv_walls[idle.adv_walls_count].x = idle.cursor_x;
        idle.adv_walls[idle.adv_walls_count].y = idle.cursor_y;
        idle.adv_walls[idle.adv_walls_count].shape = idle.piece;
        idle.adv_walls[idle.adv_walls_count].rotation = idle.rotation;
        idle.adv_walls_count++;
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

static void idle_handle_button(button_id_t id) {
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

static void process_inputs(QueueHandle_t events, bool *back_to_menu) {
    button_event_t ev;
    game.votos_arriba = 0; game.votos_abajo = 0