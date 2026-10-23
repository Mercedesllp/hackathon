#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Board-to-board link over ESP-NOW. Packets are signed with the secret set in
// menuconfig (Hackathon board demo -> Link secret); packets from boards with a
// different secret are ignored.
esp_err_t link_init(void);

// Broadcast our button bitmask (same format as buttons_read()) to the other board.
esp_err_t link_send_buttons(uint32_t pressed);

// Last button bitmask received from the other board (0 if it went quiet).
uint32_t link_remote_buttons(void);

// True if we heard a valid packet from the other board recently.
bool link_peer_alive(void);
