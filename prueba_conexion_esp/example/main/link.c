#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

#include "link.h"

static const char *TAG = "link";

// Both boards must be on the same WiFi channel.
#define LINK_CHANNEL        1
#define LINK_MAGIC          0x4B57  // "KW"
#define LINK_TIMEOUT_US     (1000 * 1000)

// HMAC-SHA256 truncated to 16 bytes: enough that nobody without the secret
// can forge a packet, and keeps packets small.
#define LINK_TAG_LEN        16
#define LINK_MAC_ALG        PSA_ALG_TRUNCATED_MAC(PSA_ALG_HMAC(PSA_ALG_SHA_256), LINK_TAG_LEN)

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint32_t pressed;
    uint8_t tag[LINK_TAG_LEN];  // signature over everything above
} link_packet_t;

#define LINK_SIGNED_LEN     offsetof(link_packet_t, tag)

static const uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static psa_key_id_t s_key;
static volatile uint32_t s_remote_pressed;
static volatile int64_t s_last_rx_us = -LINK_TIMEOUT_US;

// Runs in the WiFi task: keep it short, just check and store the data.
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len != sizeof(link_packet_t)) {
        return;
    }
    link_packet_t pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (pkt.magic != LINK_MAGIC) {
        return;
    }
    if (psa_mac_verify(s_key, LINK_MAC_ALG, (const uint8_t *)&pkt, LINK_SIGNED_LEN, pkt.tag, LINK_TAG_LEN) !=
        PSA_SUCCESS) {
        ESP_LOGD(TAG, "ignoring packet with bad signature from " MACSTR, MAC2STR(info->src_addr));
        return;
    }
    s_remote_pressed = pkt.pressed;
    s_last_rx_us = esp_timer_get_time();
}

static esp_err_t import_secret(void)
{
    const char *secret = CONFIG_HACK_LINK_SECRET;
    size_t len = strlen(secret);
    ESP_RETURN_ON_FALSE(len > 0, ESP_ERR_INVALID_STATE, TAG,
                        "link secret is empty: set it in idf.py menuconfig -> Hackathon board demo -> Link secret");

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
    psa_set_key_algorithm(&attr, LINK_MAC_ALG);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_bits(&attr, len * 8);
    psa_status_t status = psa_import_key(&attr, (const uint8_t *)secret, len, &s_key);
    psa_reset_key_attributes(&attr);
    ESP_RETURN_ON_FALSE(status == PSA_SUCCESS, ESP_FAIL, TAG, "key import failed: %d", (int)status);
    return ESP_OK;
}

esp_err_t link_init(void)
{
    ESP_RETURN_ON_ERROR(import_secret(), TAG, "no secret");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(LINK_CHANNEL, WIFI_SECOND_CHAN_NONE), TAG, "wifi channel failed");

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp-now init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(on_recv), TAG, "recv cb failed");

    esp_now_peer_info_t peer = {
        .channel = LINK_CHANNEL,
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_RETURN_ON_ERROR(esp_now_add_peer(&peer), TAG, "add peer failed");

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "ESP-NOW ready on channel %d, my MAC " MACSTR, LINK_CHANNEL, MAC2STR(mac));
    return ESP_OK;
}

esp_err_t link_send_buttons(uint32_t pressed)
{
    link_packet_t pkt = {.magic = LINK_MAGIC, .pressed = pressed};
    size_t tag_len;
    psa_status_t status =
        psa_mac_compute(s_key, LINK_MAC_ALG, (const uint8_t *)&pkt, LINK_SIGNED_LEN, pkt.tag, LINK_TAG_LEN, &tag_len);
    ESP_RETURN_ON_FALSE(status == PSA_SUCCESS, ESP_FAIL, TAG, "sign failed: %d", (int)status);
    return esp_now_send(s_broadcast_mac, (const uint8_t *)&pkt, sizeof(pkt));
}

uint32_t link_remote_buttons(void)
{
    return link_peer_alive() ? s_remote_pressed : 0;
}

bool link_peer_alive(void)
{
    return esp_timer_get_time() - s_last_rx_us < LINK_TIMEOUT_US;
}
