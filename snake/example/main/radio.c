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
    
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Partición corrupta o de una versión vieja: borrar y reintentar
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");
    
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(RADIO_CHANNEL, WIFI_SECOND_CHAN_NONE), TAG, "wifi channel failed");

    
    s_inbox = xQueueCreate(INBOX_LEN, sizeof(message_t));
    ESP_RETURN_ON_FALSE(s_inbox, ESP_ERR_NO_MEM, TAG, "no memory for inbox");

    
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp-now init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_receive), TAG, "register recv cb failed");

    
    esp_now_peer_info_t peer = {
        .channel = RADIO_CHANNEL,
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, s_broadcast, ESP_NOW_ETH_ALEN);
    ESP_RETURN_ON_ERROR(esp_now_add_peer(&peer), TAG, "add peer failed");

    ESP_LOGI(TAG, "radio lista en el canal %d", RADIO_CHANNEL);
    return ESP_OK;
}

esp_err_t radio_send(const message_t *m)
{
    // A quién, dónde empiezan los bytes, cuántos son.
    // sizeof(*m) = tamaño del struct (sizeof(m) sería el tamaño del puntero: 4)
    return esp_now_send(s_broadcast, (const uint8_t *)m, sizeof(*m));
}

bool radio_receive(message_t *m)
{
    // Saca un mensaje del buzón; el 0 es "no esperar" si está vacío
    return xQueueReceive(s_inbox, m, 0) == pdTRUE;
}
