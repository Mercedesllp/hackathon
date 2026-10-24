#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "sounds.h"
#include "audio.h"

#define WIFI_SSID "kiwi-audio"
#define WIFI_PASS "kiwikiwi"

static const char *TAG = "sounds";
static httpd_handle_t s_server = NULL;
static bool s_wifi_audio_ok = false;
static bool s_hw_audio_ok = false;

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

void sounds_play_tone(int freq_hz, int ms) {
    if (s_wifi_audio_ok) {
        wifi_audio_play_tone(freq_hz, ms);
    }
    if (s_hw_audio_ok) {
        audio_play_tone(freq_hz, ms);
    }
}

// --- MELODÍAS Y EFECTOS DE SONIDO ---

void sounds_play_eat(void) {
    sounds_play_tone(660, 50);
}

void sounds_play_wall_drop(void) {
    sounds_play_tone(990, 80);
}

void sounds_play_wall_received(void) {
    sounds_play_tone(330, 100);
}

void sounds_play_puzzle_start(void) {
    sounds_play_tone(523, 80);
}

// Melodía alegre y ascendente al ganar un bloque/puzle
void sounds_play_puzzle_win(void) {
    sounds_play_tone(523, 70); // Do
    sounds_play_tone(659, 70); // Mi
    sounds_play_tone(784, 70); // Sol
    sounds_play_tone(1046, 150); // Do alto
}

// Sonido triste al errar un puzle
void sounds_play_puzzle_lose(void) {
    sounds_play_tone(300, 120);
    sounds_play_tone(220, 200);
}

// Melodía descendente y dramática para Game Over global
void sounds_play_game_over(void) {
    sounds_play_tone(440, 120);
    sounds_play_tone(370, 120);
    sounds_play_tone(311, 150);
    sounds_play_tone(220, 300);
}

// Fanfarria triunfal para la Victoria global (Multijugador)
void sounds_play_victory(void) {
    sounds_play_tone(523, 100); // Do
    sounds_play_tone(659, 100); // Mi
    sounds_play_tone(784, 100); // Sol
    sounds_play_tone(1046, 250); // Do alto (sostenido)
    sounds_play_tone(784, 100);  // Sol
    sounds_play_tone(1046, 400); // Do alto final triunfal
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
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        }
    };
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

bool sounds_init(void) {
    s_wifi_audio_ok = wifi_audio_init();
    s_hw_audio_ok = (audio_init() == ESP_OK);
    if (!s_hw_audio_ok) {
        ESP_LOGE(TAG, "Fallo al iniciar el microfono/audio local");
    }
    return s_wifi_audio_ok || s_hw_audio_ok;
}