#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs_flash.h"

#include "radio.h"

static const char *TAG = "radio";

#define RADIO_CHANNEL 1
#define INBOX_LEN     4

static const uint8_t s_broadcast[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static QueueHandle_t s_inbox;

static void on_receive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len != sizeof(message_t)) {
        return; // no es nuestro mensaje
    }
    message_t msg;
    memcpy(&msg, data, sizeof(msg)); // bytes crudos -> struct
    xQueueSend(s_inbox, &msg, 0);    // al buzón; si está lleno se descarta (nunca esperar acá)
}

esp_err_t radio_init(void)
{
    // OMITIMOS LA INICIALIZACIÓN DE WI-FI:
    // main.c ya ejecutó wifi_audio_init() antes, por lo que el NVS, el event loop, 
    // y el Wi-Fi en modo AP (Access Point) ya están configurados y corriendo.

    s_inbox = xQueueCreate(INBOX_LEN, sizeof(message_t));
    ESP_RETURN_ON_FALSE(s_inbox, ESP_ERR_NO_MEM, TAG, "no memory for inbox");

    // Inicializamos solo ESP-NOW
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp-now init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_receive), TAG, "register recv cb failed");
    
    esp_now_peer_info_t peer = {
        .channel = RADIO_CHANNEL,
        .ifidx = WIFI_IF_AP, // <- CAMBIO CLAVE: Usamos la interfaz AP del sistema de audio
        .encrypt = false,
    };
    memcpy(peer.peer_addr, s_broadcast, ESP_NOW_ETH_ALEN);
    ESP_RETURN_ON_ERROR(esp_now_add_peer(&peer), TAG, "add peer failed");

    ESP_LOGI(TAG, "radio lista en el canal %d (usando WiFi AP del audio)", RADIO_CHANNEL);
    return ESP_OK;
}

esp_err_t radio_send(const message_t *m)
{
    return esp_now_send(s_broadcast, (const uint8_t *)m, sizeof(*m));
}

bool radio_receive(message_t *m)
{
    return xQueueReceive(s_inbox, m, 0) == pdTRUE;
}