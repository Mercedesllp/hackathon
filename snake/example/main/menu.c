// #include "menu.h"

// #include "esp_check.h"

// #include "buttons.h"
// #include "display.h"

// static esp_err_t render_menu(bool display_ok)
// {
//     if (!display_ok) {
//         return ESP_OK;
//     }

//     display_clear();
//     display_text(52, 12, "Menu", false);
//     display_set_pixel(70, 10, true);
//     display_set_pixel(71, 9, true);
//     display_text(49, 36, "start", true);
//     return display_flush();
// }

// esp_err_t menu_wait_for_start(QueueHandle_t events, bool display_ok)
// {
//     if (events == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     ESP_RETURN_ON_ERROR(render_menu(display_ok), "menu", "menu render failed");

//     button_event_t event;
//     while (xQueueReceive(events, &event, portMAX_DELAY) == pdTRUE) {
//         if (event.pressed) {
//             return ESP_OK;
//         }
//     }

//     return ESP_FAIL;
// }
