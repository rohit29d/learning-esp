#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// ─── Display dimensions ────────────────────────────────────
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_PAGES  (OLED_HEIGHT / 8)   // 8 pages for 64-pixel display

// ─── Drawing quadrant flags (for circle/disc) ──────────────
#define OLED_DRAW_UPPER_RIGHT 0x01
#define OLED_DRAW_UPPER_LEFT  0x02
#define OLED_DRAW_LOWER_LEFT  0x04
#define OLED_DRAW_LOWER_RIGHT 0x08
#define OLED_DRAW_ALL         0x0F

// ─── Scroll types ──────────────────────────────────────────
typedef enum {
    SCROLL_RIGHT = 1,
    SCROLL_LEFT,
    SCROLL_UP,
    SCROLL_DOWN,
    PAGE_SCROLL_DOWN,
    PAGE_SCROLL_UP,
} oled_scroll_type_t;

// ─── Page structure (one 128-byte segment row) ─────────────
typedef struct {
    uint8_t _segs[128];
} PAGE_t;

// ─── OLED device handle ────────────────────────────────────
typedef struct {
    int      _width;
    int      _height;
    int      _pages;
    bool     _flip;
    PAGE_t   _page[OLED_PAGES];

    // Software-scroll state
    bool     _scEnable;
    int      _scStart;
    int      _scEnd;
    int      _scDirection;
} i2c_oled_t;

// ═══════════════════════════════════════════════════════════
//  Low-level I2C transport
// ═══════════════════════════════════════════════════════════
esp_err_t i2c_oled_hw_init(void);
void      i2c_oled_send_cmd(const uint8_t *cmd, size_t len);
void      i2c_oled_send_data(const uint8_t *data, size_t len);
void      i2c_oled_display_page(i2c_oled_t *dev, int page, int seg,
                                const uint8_t *data, int width);

// ═══════════════════════════════════════════════════════════
//  Initialization
// ═══════════════════════════════════════════════════════════
esp_err_t i2c_oled_init(i2c_oled_t *dev, int width, int height);

// ═══════════════════════════════════════════════════════════
//  Buffer management
// ═══════════════════════════════════════════════════════════
void i2c_oled_show_buffer(i2c_oled_t *dev);
void i2c_oled_set_buffer(i2c_oled_t *dev, const uint8_t *buffer);
void i2c_oled_get_buffer(i2c_oled_t *dev, uint8_t *buffer);
void i2c_oled_set_page(i2c_oled_t *dev, int page, const uint8_t *buffer);
void i2c_oled_get_page(i2c_oled_t *dev, int page, uint8_t *buffer);

// ═══════════════════════════════════════════════════════════
//  Display primitives
// ═══════════════════════════════════════════════════════════
void i2c_oled_display_image(i2c_oled_t *dev, int page, int seg,
                            const uint8_t *images, int width);
void i2c_oled_display_text(i2c_oled_t *dev, int page, const char *text,
                           int text_len, bool invert);
void i2c_oled_display_text_box1(i2c_oled_t *dev, int page, int seg,
                                const char *text, int box_width,
                                int text_len, bool invert, int delay);
void i2c_oled_display_text_box2(i2c_oled_t *dev, int page, int seg,
                                const char *text, int box_width,
                                int text_len, bool invert, int delay);
void i2c_oled_display_text_x3(i2c_oled_t *dev, int page, const char *text,
                              int text_len, bool invert);
void i2c_oled_display_rotate_text(i2c_oled_t *dev, int seg, const char *text,
                                  int text_len, bool invert);

// ═══════════════════════════════════════════════════════════
//  Screen / line clear
// ═══════════════════════════════════════════════════════════
void i2c_oled_clear_screen(i2c_oled_t *dev, bool invert);
void i2c_oled_clear_line(i2c_oled_t *dev, int page, bool invert);

// ═══════════════════════════════════════════════════════════
//  Contrast
// ═══════════════════════════════════════════════════════════
void i2c_oled_contrast(i2c_oled_t *dev, int contrast);

// ═══════════════════════════════════════════════════════════
//  Software scroll
// ═══════════════════════════════════════════════════════════
void i2c_oled_software_scroll(i2c_oled_t *dev, int start, int end);
void i2c_oled_scroll_text(i2c_oled_t *dev, const char *text,
                          int text_len, bool invert);
void i2c_oled_scroll_clear(i2c_oled_t *dev);

// ═══════════════════════════════════════════════════════════
//  Wrap-around (pixel & page scrolling in buffer)
// ═══════════════════════════════════════════════════════════
void i2c_oled_wrap_arround(i2c_oled_t *dev, oled_scroll_type_t scroll,
                           int start, int end, int8_t delay);

// ═══════════════════════════════════════════════════════════
//  Bitmaps
// ═══════════════════════════════════════════════════════════
void i2c_oled_bitmaps(i2c_oled_t *dev, int xpos, int ypos,
                      const uint8_t *bitmap, int width, int height,
                      bool invert);

// ═══════════════════════════════════════════════════════════
//  Drawing primitives (write to buffer only — call
//  i2c_oled_show_buffer() to push to display)
// ═══════════════════════════════════════════════════════════
void i2c_oled_pixel(i2c_oled_t *dev, int xpos, int ypos, bool invert);
void i2c_oled_line(i2c_oled_t *dev, int x1, int y1, int x2, int y2,
                   bool invert);
void i2c_oled_circle(i2c_oled_t *dev, int x0, int y0, int r,
                     unsigned int opt, bool invert);
void i2c_oled_disc(i2c_oled_t *dev, int x0, int y0, int r,
                   unsigned int opt, bool invert);
void i2c_oled_cursor(i2c_oled_t *dev, int x0, int y0, int r, bool invert);

// ═══════════════════════════════════════════════════════════
//  Effects
// ═══════════════════════════════════════════════════════════
void i2c_oled_fadeout(i2c_oled_t *dev);

// ═══════════════════════════════════════════════════════════
//  Bitmap draw for GIF playback (flat 1024-byte page buffer)
// ═══════════════════════════════════════════════════════════
void i2c_oled_draw_bitmap_raw(uint16_t x, uint16_t y,
                              const uint8_t *bitmap,
                              uint16_t w, uint16_t h);
void i2c_oled_refresh(void);

// ═══════════════════════════════════════════════════════════
//  Utility helpers
// ═══════════════════════════════════════════════════════════
void    i2c_oled_invert(uint8_t *buf, size_t blen);
void    i2c_oled_flip(uint8_t *buf, size_t blen);
uint8_t i2c_oled_copy_bit(uint8_t src, int srcBits,
                          uint8_t dst, int dstBits);
uint8_t i2c_oled_rotate_byte(uint8_t ch);
void    i2c_oled_rotate_image(uint8_t *image, bool flip);

// ═══════════════════════════════════════════════════════════
//  Debug
// ═══════════════════════════════════════════════════════════
void i2c_oled_dump(i2c_oled_t dev);
void i2c_oled_dump_page(i2c_oled_t *dev, int page, int seg);
