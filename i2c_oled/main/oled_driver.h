#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_DRIVER_WIDTH  CONFIG_OLED_DRIVER_WIDTH
#define OLED_DRIVER_HEIGHT CONFIG_OLED_DRIVER_HEIGHT

esp_err_t oled_driver_init(void);
esp_err_t oled_driver_deinit(void);
esp_err_t oled_driver_flush(void);

void oled_driver_clear(void);
void oled_driver_set_pixel(int x, int y, bool on);
void oled_driver_draw_char(int x, int y, char c);
void oled_driver_draw_text(int x, int y, const char *text);
void oled_driver_draw_frame(void);
uint8_t *oled_driver_get_buffer(void);

#ifdef __cplusplus
}
#endif
