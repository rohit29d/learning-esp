#include "esp_err.h"
#include "oled_driver.h"

void app_main(void)
{
    ESP_ERROR_CHECK(oled_driver_init());
    oled_driver_clear();
    oled_driver_draw_frame();
    oled_driver_draw_text(8, 16, "SSD1306");
    oled_driver_draw_text(8, 32, "128X64");
    ESP_ERROR_CHECK(oled_driver_flush());
}
