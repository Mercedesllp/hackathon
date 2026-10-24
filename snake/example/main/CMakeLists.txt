idf_component_register(
    SRCS "main.c" "menu.c" "display.c" "audio.c" "buttons.c" "snake.c" "radio.c" "walls.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2s esp_driver_gpio esp_wifi esp_netif esp_event nvs_flash esp_wifi esp_netif esp_event nvs_flash esp_http_server
)
