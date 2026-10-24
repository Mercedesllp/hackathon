idf_component_register(
    SRCS 
        "main.c"
        "display.c"
        "audio.c"
        "buttons.c"
        "sounds.c"
        "radio.c"
        "walls.c"
        "snake.c"
        "menu.c"
        "minigames.c"
        "block_placer.c"
        "game_1p.c"
        "game_2p.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2s esp_driver_gpio esp_wifi esp_netif esp_event esp_timer nvs_flash mbedtls esp_http_server
)