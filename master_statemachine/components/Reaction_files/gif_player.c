#include "gif_player.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <stddef.h>

/* Use the i2c_oled driver functions for display */
extern void i2c_oled_draw_bitmap_raw(uint16_t x, uint16_t y,
                                     const uint8_t *bitmap,
                                     uint16_t w, uint16_t h);
extern void i2c_oled_refresh(void);

static const AnimatedGIF* current_gif = NULL;
static uint8_t target_loop_count = 0;
static uint8_t current_loop = 0;
static uint8_t current_frame = 0;
static int64_t next_frame_time_us = 0;

void playGIF(const AnimatedGIF* gif, uint8_t loop_count) {
    for (uint8_t loop = 0; loop < loop_count; loop++) {
        for (uint8_t i = 0; i < gif->frame_count; i++) {
            i2c_oled_draw_bitmap_raw(0, 0, gif->frames[i],
                                     gif->width, gif->height);
            i2c_oled_refresh();
            vTaskDelay(pdMS_TO_TICKS(gif->delays[i]));
        }
    }
}

void gif_set(const AnimatedGIF* gif, uint8_t loop_count) {
    current_gif = gif;
    target_loop_count = loop_count;
    current_loop = 0;
    current_frame = 0;
    next_frame_time_us = esp_timer_get_time(); // Trigger first frame immediately
}

void gif_tick(void) {
    if (current_gif == NULL) return;

    int64_t now = esp_timer_get_time();
    if (now >= next_frame_time_us) {
        // Draw current frame
        i2c_oled_draw_bitmap_raw(0, 0, current_gif->frames[current_frame],
                                 current_gif->width, current_gif->height);
        i2c_oled_refresh();

        // Schedule next frame
        next_frame_time_us = now + ((int64_t)current_gif->delays[current_frame] * 1000);

        // Advance frame
        current_frame++;
        if (current_frame >= current_gif->frame_count) {
            current_frame = 0;
            current_loop++;
            if (target_loop_count > 0 && current_loop >= target_loop_count) {
                // Animation finished
                current_gif = NULL;
            }
        }
    }
}

void gif_stop(void) {
    current_gif = NULL;
}

bool gif_is_playing(void) {
    return (current_gif != NULL);
}
