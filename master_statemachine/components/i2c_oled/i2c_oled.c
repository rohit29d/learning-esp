/*
 * i2c_oled.c — Full SSD1306 I2C OLED driver
 *
 * Integrates:  I2C transport, display init, text rendering (8×8 font),
 *              3× text, scrolling text boxes, bitmap drawing, pixel/line/
 *              circle/disc/cursor primitives, software scroll, wrap-around,
 *              contrast control, fade-out effect, and raw GIF-frame bitmap
 *              drawing for the gif_player component.
 */

#include "i2c_oled.h"

#include <string.h>

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ─── I2C hardware configuration ──────────────────────────── */
#define I2C_MASTER_SCL_IO 9
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0
#define I2C_MASTER_TIMEOUT_MS 1000
#define SSD1306_ADDR 0x3C

static const char *TAG = "I2C_OLED";

/* ─── Alignment helper for out_column_t ──────────────────── */
#define PACK8 __attribute__((aligned(__alignof__(uint8_t)), packed))

typedef union out_column_t {
  uint32_t u32;
  uint8_t u8[4];
} PACK8 out_column_t;

/* ─── Flat framebuffer for raw GIF playback ──────────────── */
static uint8_t display_buffer[1024];

/* ═══════════════════════════════════════════════════════════
 *  Embedded 8×8 font  (transposed for column-major SSD1306)
 * ═══════════════════════════════════════════════════════════ */
static const uint8_t font8x8_basic_tr[128][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0000 (nul)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0001
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0002
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0003
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0004
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0005
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0006
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0007
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0008
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0009
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000B
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000C
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000D
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000E
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000F
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0010
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0011
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0012
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0013
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0014
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0015
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0016
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0017
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0018
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0019
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001B
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001C
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001D
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001E
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001F
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0020 (space)
    {0x00, 0x00, 0x06, 0x5F, 0x5F, 0x06, 0x00, 0x00}, // U+0021 (!)
    {0x00, 0x03, 0x03, 0x00, 0x03, 0x03, 0x00, 0x00}, // U+0022 (")
    {0x14, 0x7F, 0x7F, 0x14, 0x7F, 0x7F, 0x14, 0x00}, // U+0023 (#)
    {0x24, 0x2E, 0x6B, 0x6B, 0x3A, 0x12, 0x00, 0x00}, // U+0024 ($)
    {0x46, 0x66, 0x30, 0x18, 0x0C, 0x66, 0x62, 0x00}, // U+0025 (%)
    {0x30, 0x7A, 0x4F, 0x5D, 0x37, 0x7A, 0x48, 0x00}, // U+0026 (&)
    {0x04, 0x07, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0027 (')
    {0x00, 0x1C, 0x3E, 0x63, 0x41, 0x00, 0x00, 0x00}, // U+0028 (()
    {0x00, 0x41, 0x63, 0x3E, 0x1C, 0x00, 0x00, 0x00}, // U+0029 ())
    {0x08, 0x2A, 0x3E, 0x1C, 0x1C, 0x3E, 0x2A, 0x08}, // U+002A (*)
    {0x08, 0x08, 0x3E, 0x3E, 0x08, 0x08, 0x00, 0x00}, // U+002B (+)
    {0x00, 0x80, 0xE0, 0x60, 0x00, 0x00, 0x00, 0x00}, // U+002C (,)
    {0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00}, // U+002D (-)
    {0x00, 0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00}, // U+002E (.)
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // U+002F (/)
    {0x3E, 0x7F, 0x71, 0x59, 0x4D, 0x7F, 0x3E, 0x00}, // U+0030 (0)
    {0x40, 0x42, 0x7F, 0x7F, 0x40, 0x40, 0x00, 0x00}, // U+0031 (1)
    {0x62, 0x73, 0x59, 0x49, 0x6F, 0x66, 0x00, 0x00}, // U+0032 (2)
    {0x22, 0x63, 0x49, 0x49, 0x7F, 0x36, 0x00, 0x00}, // U+0033 (3)
    {0x18, 0x1C, 0x16, 0x53, 0x7F, 0x7F, 0x50, 0x00}, // U+0034 (4)
    {0x27, 0x67, 0x45, 0x45, 0x7D, 0x39, 0x00, 0x00}, // U+0035 (5)
    {0x3C, 0x7E, 0x4B, 0x49, 0x79, 0x30, 0x00, 0x00}, // U+0036 (6)
    {0x03, 0x03, 0x71, 0x79, 0x0F, 0x07, 0x00, 0x00}, // U+0037 (7)
    {0x36, 0x7F, 0x49, 0x49, 0x7F, 0x36, 0x00, 0x00}, // U+0038 (8)
    {0x06, 0x4F, 0x49, 0x69, 0x3F, 0x1E, 0x00, 0x00}, // U+0039 (9)
    {0x00, 0x00, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00}, // U+003A (:)
    {0x00, 0x80, 0xE6, 0x66, 0x00, 0x00, 0x00, 0x00}, // U+003B (;)
    {0x08, 0x1C, 0x36, 0x63, 0x41, 0x00, 0x00, 0x00}, // U+003C (<)
    {0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x00, 0x00}, // U+003D (=)
    {0x00, 0x41, 0x63, 0x36, 0x1C, 0x08, 0x00, 0x00}, // U+003E (>)
    {0x02, 0x03, 0x51, 0x59, 0x0F, 0x06, 0x00, 0x00}, // U+003F (?)
    {0x3E, 0x7F, 0x41, 0x5D, 0x5D, 0x1F, 0x1E, 0x00}, // U+0040 (@)
    {0x7C, 0x7E, 0x13, 0x13, 0x7E, 0x7C, 0x00, 0x00}, // U+0041 (A)
    {0x41, 0x7F, 0x7F, 0x49, 0x49, 0x7F, 0x36, 0x00}, // U+0042 (B)
    {0x1C, 0x3E, 0x63, 0x41, 0x41, 0x63, 0x22, 0x00}, // U+0043 (C)
    {0x41, 0x7F, 0x7F, 0x41, 0x63, 0x3E, 0x1C, 0x00}, // U+0044 (D)
    {0x41, 0x7F, 0x7F, 0x49, 0x5D, 0x41, 0x63, 0x00}, // U+0045 (E)
    {0x41, 0x7F, 0x7F, 0x49, 0x1D, 0x01, 0x03, 0x00}, // U+0046 (F)
    {0x1C, 0x3E, 0x63, 0x41, 0x51, 0x73, 0x72, 0x00}, // U+0047 (G)
    {0x7F, 0x7F, 0x08, 0x08, 0x7F, 0x7F, 0x00, 0x00}, // U+0048 (H)
    {0x00, 0x41, 0x7F, 0x7F, 0x41, 0x00, 0x00, 0x00}, // U+0049 (I)
    {0x30, 0x70, 0x40, 0x41, 0x7F, 0x3F, 0x01, 0x00}, // U+004A (J)
    {0x41, 0x7F, 0x7F, 0x08, 0x1C, 0x77, 0x63, 0x00}, // U+004B (K)
    {0x41, 0x7F, 0x7F, 0x41, 0x40, 0x60, 0x70, 0x00}, // U+004C (L)
    {0x7F, 0x7F, 0x0E, 0x1C, 0x0E, 0x7F, 0x7F, 0x00}, // U+004D (M)
    {0x7F, 0x7F, 0x06, 0x0C, 0x18, 0x7F, 0x7F, 0x00}, // U+004E (N)
    {0x1C, 0x3E, 0x63, 0x41, 0x63, 0x3E, 0x1C, 0x00}, // U+004F (O)
    {0x41, 0x7F, 0x7F, 0x49, 0x09, 0x0F, 0x06, 0x00}, // U+0050 (P)
    {0x1E, 0x3F, 0x21, 0x71, 0x7F, 0x5E, 0x00, 0x00}, // U+0051 (Q)
    {0x41, 0x7F, 0x7F, 0x09, 0x19, 0x7F, 0x66, 0x00}, // U+0052 (R)
    {0x26, 0x6F, 0x4D, 0x59, 0x73, 0x32, 0x00, 0x00}, // U+0053 (S)
    {0x03, 0x41, 0x7F, 0x7F, 0x41, 0x03, 0x00, 0x00}, // U+0054 (T)
    {0x7F, 0x7F, 0x40, 0x40, 0x7F, 0x7F, 0x00, 0x00}, // U+0055 (U)
    {0x1F, 0x3F, 0x60, 0x60, 0x3F, 0x1F, 0x00, 0x00}, // U+0056 (V)
    {0x7F, 0x7F, 0x30, 0x18, 0x30, 0x7F, 0x7F, 0x00}, // U+0057 (W)
    {0x43, 0x67, 0x3C, 0x18, 0x3C, 0x67, 0x43, 0x00}, // U+0058 (X)
    {0x07, 0x4F, 0x78, 0x78, 0x4F, 0x07, 0x00, 0x00}, // U+0059 (Y)
    {0x47, 0x63, 0x71, 0x59, 0x4D, 0x67, 0x73, 0x00}, // U+005A (Z)
    {0x00, 0x7F, 0x7F, 0x41, 0x41, 0x00, 0x00, 0x00}, // U+005B ([)
    {0x01, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00}, // U+005C (\)
    {0x00, 0x41, 0x41, 0x7F, 0x7F, 0x00, 0x00, 0x00}, // U+005D (])
    {0x08, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x08, 0x00}, // U+005E (^)
    {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}, // U+005F (_)
    {0x00, 0x00, 0x03, 0x07, 0x04, 0x00, 0x00, 0x00}, // U+0060 (`)
    {0x20, 0x74, 0x54, 0x54, 0x3C, 0x78, 0x40, 0x00}, // U+0061 (a)
    {0x41, 0x7F, 0x3F, 0x48, 0x48, 0x78, 0x30, 0x00}, // U+0062 (b)
    {0x38, 0x7C, 0x44, 0x44, 0x6C, 0x28, 0x00, 0x00}, // U+0063 (c)
    {0x30, 0x78, 0x48, 0x49, 0x3F, 0x7F, 0x40, 0x00}, // U+0064 (d)
    {0x38, 0x7C, 0x54, 0x54, 0x5C, 0x18, 0x00, 0x00}, // U+0065 (e)
    {0x48, 0x7E, 0x7F, 0x49, 0x03, 0x02, 0x00, 0x00}, // U+0066 (f)
    {0x98, 0xBC, 0xA4, 0xA4, 0xF8, 0x7C, 0x04, 0x00}, // U+0067 (g)
    {0x41, 0x7F, 0x7F, 0x08, 0x04, 0x7C, 0x78, 0x00}, // U+0068 (h)
    {0x00, 0x44, 0x7D, 0x7D, 0x40, 0x00, 0x00, 0x00}, // U+0069 (i)
    {0x60, 0xE0, 0x80, 0x80, 0xFD, 0x7D, 0x00, 0x00}, // U+006A (j)
    {0x41, 0x7F, 0x7F, 0x10, 0x38, 0x6C, 0x44, 0x00}, // U+006B (k)
    {0x00, 0x41, 0x7F, 0x7F, 0x40, 0x00, 0x00, 0x00}, // U+006C (l)
    {0x7C, 0x7C, 0x18, 0x38, 0x1C, 0x7C, 0x78, 0x00}, // U+006D (m)
    {0x7C, 0x7C, 0x04, 0x04, 0x7C, 0x78, 0x00, 0x00}, // U+006E (n)
    {0x38, 0x7C, 0x44, 0x44, 0x7C, 0x38, 0x00, 0x00}, // U+006F (o)
    {0x84, 0xFC, 0xF8, 0xA4, 0x24, 0x3C, 0x18, 0x00}, // U+0070 (p)
    {0x18, 0x3C, 0x24, 0xA4, 0xF8, 0xFC, 0x84, 0x00}, // U+0071 (q)
    {0x44, 0x7C, 0x78, 0x4C, 0x04, 0x1C, 0x18, 0x00}, // U+0072 (r)
    {0x48, 0x5C, 0x54, 0x54, 0x74, 0x24, 0x00, 0x00}, // U+0073 (s)
    {0x00, 0x04, 0x3E, 0x7F, 0x44, 0x24, 0x00, 0x00}, // U+0074 (t)
    {0x3C, 0x7C, 0x40, 0x40, 0x3C, 0x7C, 0x40, 0x00}, // U+0075 (u)
    {0x1C, 0x3C, 0x60, 0x60, 0x3C, 0x1C, 0x00, 0x00}, // U+0076 (v)
    {0x3C, 0x7C, 0x70, 0x38, 0x70, 0x7C, 0x3C, 0x00}, // U+0077 (w)
    {0x44, 0x6C, 0x38, 0x10, 0x38, 0x6C, 0x44, 0x00}, // U+0078 (x)
    {0x9C, 0xBC, 0xA0, 0xA0, 0xFC, 0x7C, 0x00, 0x00}, // U+0079 (y)
    {0x4C, 0x64, 0x74, 0x5C, 0x4C, 0x64, 0x00, 0x00}, // U+007A (z)
    {0x08, 0x08, 0x3E, 0x77, 0x41, 0x41, 0x00, 0x00}, // U+007B ({)
    {0x00, 0x00, 0x00, 0x77, 0x77, 0x00, 0x00, 0x00}, // U+007C (|)
    {0x41, 0x41, 0x77, 0x3E, 0x08, 0x08, 0x00, 0x00}, // U+007D (})
    {0x02, 0x03, 0x01, 0x03, 0x02, 0x03, 0x01, 0x00}, // U+007E (~)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+007F
};

/* ═══════════════════════════════════════════════════════════
 *  Low-level I2C transport
 * ═══════════════════════════════════════════════════════════ */

esp_err_t i2c_oled_hw_init(void) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = I2C_MASTER_FREQ_HZ,
  };
  esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
  if (ret != ESP_OK)
    return ret;
  return i2c_driver_install(I2C_MASTER_NUM, conf.mode,
                            I2C_MASTER_RX_BUF_DISABLE,
                            I2C_MASTER_TX_BUF_DISABLE, 0);
}

void i2c_oled_send_cmd(const uint8_t *cmd, size_t len) {
  uint8_t buf[len + 1];
  buf[0] = 0x00; // Co=0, D/C#=0  (command)
  memcpy(&buf[1], cmd, len);
  i2c_master_write_to_device(I2C_MASTER_NUM, SSD1306_ADDR, buf, len + 1,
                             I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

void i2c_oled_send_data(const uint8_t *data, size_t len) {
  uint8_t buf[len + 1];
  buf[0] = 0x40; // Co=0, D/C#=1  (data)
  memcpy(&buf[1], data, len);
  i2c_master_write_to_device(I2C_MASTER_NUM, SSD1306_ADDR, buf, len + 1,
                             I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/* Send page-addressed data starting at (page, seg) */
void i2c_oled_display_page(i2c_oled_t *dev, int page, int seg,
                           const uint8_t *data, int width) {
  (void)dev;
  uint8_t cmds[3];
  cmds[0] = 0xB0 | page;       // page start address
  cmds[1] = seg & 0x0F;        // lower column nibble
  cmds[2] = 0x10 | (seg >> 4); // upper column nibble
  i2c_oled_send_cmd(cmds, 3);
  i2c_oled_send_data(data, width);
}

/* ═══════════════════════════════════════════════════════════
 *  Initialization
 * ═══════════════════════════════════════════════════════════ */

esp_err_t i2c_oled_init(i2c_oled_t *dev, int width, int height) {
  esp_err_t ret = i2c_oled_hw_init();
  if (ret != ESP_OK)
    return ret;

  const uint8_t init_cmds[] = {
      0xAE,                        // Display off
      0x20, 0x02,                  // Page Addressing Mode
      0xB0,                        // Page Start Address = 0
      0xC8,                        // COM Output Scan Direction: remapped
      0x00,                        // Low column address
      0x10,                        // High column address
      0x40,                        // Start line = 0
      0x81, 0x7F,                  // Contrast
      0xA1,                        // Segment Re-map
      0xA6,                        // Normal display (not inverted)
      0xA8, (uint8_t)(height - 1), // Multiplex ratio
      0xA4,                        // Output follows RAM
      0xD3, 0x00,                  // Display offset = 0
      0xD5, 0xF0,                  // Osc frequency
      0xD9, 0x22,                  // Pre-charge period
      0xDA, 0x12,                  // COM pins hardware config
      0xDB, 0x20,                  // VCOMH deselect level
      0x8D, 0x14,                  // Charge pump enable
      0xAF                         // Display ON
  };
  i2c_oled_send_cmd(init_cmds, sizeof(init_cmds));

  dev->_width = width;
  dev->_height = height;
  dev->_pages = height / 8;
  dev->_flip = false;
  dev->_scEnable = false;

  // Clear internal buffer
  for (int i = 0; i < dev->_pages; i++) {
    memset(dev->_page[i]._segs, 0, 128);
  }

  ESP_LOGI(TAG, "SSD1306 %dx%d initialized over I2C", width, height);
  return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════
 *  Buffer management
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_show_buffer(i2c_oled_t *dev) {
  for (int page = 0; page < dev->_pages; page++) {
    i2c_oled_display_page(dev, page, 0, dev->_page[page]._segs, dev->_width);
  }
}

void i2c_oled_set_buffer(i2c_oled_t *dev, const uint8_t *buffer) {
  int index = 0;
  for (int page = 0; page < dev->_pages; page++) {
    memcpy(dev->_page[page]._segs, &buffer[index], 128);
    index += 128;
  }
}

void i2c_oled_get_buffer(i2c_oled_t *dev, uint8_t *buffer) {
  int index = 0;
  for (int page = 0; page < dev->_pages; page++) {
    memcpy(&buffer[index], dev->_page[page]._segs, 128);
    index += 128;
  }
}

void i2c_oled_set_page(i2c_oled_t *dev, int page, const uint8_t *buffer) {
  memcpy(dev->_page[page]._segs, buffer, 128);
}

void i2c_oled_get_page(i2c_oled_t *dev, int page, uint8_t *buffer) {
  memcpy(buffer, dev->_page[page]._segs, 128);
}

/* ═══════════════════════════════════════════════════════════
 *  Utility helpers
 * ═══════════════════════════════════════════════════════════ */

uint8_t i2c_oled_rotate_byte(uint8_t ch1) {
  uint8_t ch2 = 0;
  for (int j = 0; j < 8; j++) {
    ch2 = (ch2 << 1) + (ch1 & 0x01);
    ch1 >>= 1;
  }
  return ch2;
}

void i2c_oled_invert(uint8_t *buf, size_t blen) {
  for (size_t i = 0; i < blen; i++) {
    buf[i] = ~buf[i];
  }
}

void i2c_oled_flip(uint8_t *buf, size_t blen) {
  for (size_t i = 0; i < blen; i++) {
    buf[i] = i2c_oled_rotate_byte(buf[i]);
  }
}

uint8_t i2c_oled_copy_bit(uint8_t src, int srcBits, uint8_t dst, int dstBits) {
  uint8_t smask = 0x01 << srcBits;
  uint8_t dmask = 0x01 << dstBits;
  uint8_t _src = src & smask;
  if (_src != 0) {
    return dst | dmask; // set bit
  } else {
    return dst & ~dmask; // clear bit
  }
}

/* Rotate an 8×8 character image (transpose) */
void i2c_oled_rotate_image(uint8_t *image, bool flip) {
  uint8_t _image[8];
  uint8_t _smask = 0x01;
  for (int i = 0; i < 8; i++) {
    uint8_t _dmask = 0x80;
    _image[i] = 0;
    for (int j = 0; j < 8; j++) {
      if (image[j] & _smask) {
        _image[i] += _dmask;
      }
      _dmask >>= 1;
    }
    _smask <<= 1;
  }
  memcpy(image, _image, 8);
  if (flip)
    i2c_oled_flip(image, 8);
}

/* ═══════════════════════════════════════════════════════════
 *  Display primitives
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_display_image(i2c_oled_t *dev, int page, int seg,
                            const uint8_t *images, int width) {
  i2c_oled_display_page(dev, page, seg, images, width);
  // Mirror to internal buffer
  memcpy(&dev->_page[page]._segs[seg], images, width);
}

void i2c_oled_display_text(i2c_oled_t *dev, int page, const char *text,
                           int text_len, bool invert) {
  if (page >= dev->_pages)
    return;
  int _text_len = text_len;
  if (_text_len > 16)
    _text_len = 16;

  int seg = 0;
  uint8_t image[8];
  for (int i = 0; i < _text_len; i++) {
    memcpy(image, font8x8_basic_tr[(uint8_t)text[i]], 8);
    if (invert)
      i2c_oled_invert(image, 8);
    if (dev->_flip)
      i2c_oled_flip(image, 8);
    i2c_oled_display_image(dev, page, seg, image, 8);
    seg += 8;
  }
}

void i2c_oled_display_text_box1(i2c_oled_t *dev, int page, int seg,
                                const char *text, int box_width, int text_len,
                                bool invert, int delay) {
  if (page >= dev->_pages)
    return;
  int text_box_pixel = box_width * 8;
  if (seg + text_box_pixel > dev->_width)
    return;

  int _seg = seg;
  uint8_t image[8];
  for (int i = 0; i < box_width; i++) {
    memcpy(image, font8x8_basic_tr[(uint8_t)text[i]], 8);
    if (invert)
      i2c_oled_invert(image, 8);
    if (dev->_flip)
      i2c_oled_flip(image, 8);
    i2c_oled_display_image(dev, page, _seg, image, 8);
    _seg += 8;
  }
  vTaskDelay(delay);

  // Horizontally scroll inside the box
  for (int _text = box_width; _text < text_len; _text++) {
    memcpy(image, font8x8_basic_tr[(uint8_t)text[_text]], 8);
    if (invert)
      i2c_oled_invert(image, 8);
    if (dev->_flip)
      i2c_oled_flip(image, 8);
    for (int _bit = 0; _bit < 8; _bit++) {
      for (int _pixel = 0; _pixel < text_box_pixel; _pixel++) {
        dev->_page[page]._segs[_pixel + seg] =
            dev->_page[page]._segs[_pixel + seg + 1];
      }
      dev->_page[page]._segs[seg + text_box_pixel - 1] = image[_bit];
      i2c_oled_display_image(dev, page, seg, &dev->_page[page]._segs[seg],
                             text_box_pixel);
      vTaskDelay(delay);
    }
  }
}

void i2c_oled_display_text_box2(i2c_oled_t *dev, int page, int seg,
                                const char *text, int box_width, int text_len,
                                bool invert, int delay) {
  if (page >= dev->_pages)
    return;
  int text_box_pixel = box_width * 8;
  if (seg + text_box_pixel > dev->_width)
    return;

  int _seg = seg;
  uint8_t image[8];

  // Fill the text box with blanks
  for (int i = 0; i < box_width; i++) {
    memcpy(image, font8x8_basic_tr[0x20], 8);
    if (invert)
      i2c_oled_invert(image, 8);
    if (dev->_flip)
      i2c_oled_flip(image, 8);
    i2c_oled_display_image(dev, page, _seg, image, 8);
    _seg += 8;
  }
  vTaskDelay(delay);

  // Scroll text in
  for (int _text = 0; _text < text_len; _text++) {
    memcpy(image, font8x8_basic_tr[(uint8_t)text[_text]], 8);
    if (invert)
      i2c_oled_invert(image, 8);
    if (dev->_flip)
      i2c_oled_flip(image, 8);
    for (int _bit = 0; _bit < 8; _bit++) {
      for (int _pixel = 0; _pixel < text_box_pixel; _pixel++) {
        dev->_page[page]._segs[_pixel + seg] =
            dev->_page[page]._segs[_pixel + seg + 1];
      }
      dev->_page[page]._segs[seg + text_box_pixel - 1] = image[_bit];
      i2c_oled_display_image(dev, page, seg, &dev->_page[page]._segs[seg],
                             text_box_pixel);
      vTaskDelay(delay);
    }
  }

  // Scroll blanks to clear
  for (int _text = 0; _text < box_width; _text++) {
    memcpy(image, font8x8_basic_tr[0x20], 8);
    if (invert)
      i2c_oled_invert(image, 8);
    if (dev->_flip)
      i2c_oled_flip(image, 8);
    for (int _bit = 0; _bit < 8; _bit++) {
      for (int _pixel = 0; _pixel < text_box_pixel; _pixel++) {
        dev->_page[page]._segs[_pixel + seg] =
            dev->_page[page]._segs[_pixel + seg + 1];
      }
      dev->_page[page]._segs[seg + text_box_pixel - 1] = image[_bit];
      i2c_oled_display_image(dev, page, seg, &dev->_page[page]._segs[seg],
                             text_box_pixel);
      vTaskDelay(delay);
    }
  }
}

/* 3× enlarged text (by Coert Vonk) */
void i2c_oled_display_text_x3(i2c_oled_t *dev, int page, const char *text,
                              int text_len, bool invert) {
  if (page >= dev->_pages)
    return;
  int _text_len = text_len;
  if (_text_len > 5)
    _text_len = 5;

  int seg = 0;
  for (int nn = 0; nn < _text_len; nn++) {
    const uint8_t *in_columns = font8x8_basic_tr[(uint8_t)text[nn]];

    // Make the character 3× as high
    out_column_t out_columns[8];
    memset(out_columns, 0, sizeof(out_columns));

    for (int xx = 0; xx < 8; xx++) {
      uint32_t in_bitmask = 0b1;
      uint32_t out_bitmask = 0b111;
      for (int yy = 0; yy < 8; yy++) {
        if (in_columns[xx] & in_bitmask) {
          out_columns[xx].u32 |= out_bitmask;
        }
        in_bitmask <<= 1;
        out_bitmask <<= 3;
      }
    }

    // Render in 8-column-high pieces, 3× as wide
    for (int yy = 0; yy < 3; yy++) {
      uint8_t image[24];
      for (int xx = 0; xx < 8; xx++) {
        image[xx * 3 + 0] = image[xx * 3 + 1] = image[xx * 3 + 2] =
            out_columns[xx].u8[yy];
      }
      if (invert)
        i2c_oled_invert(image, 24);
      if (dev->_flip)
        i2c_oled_flip(image, 24);
      i2c_oled_display_page(dev, page + yy, seg, image, 24);
      memcpy(&dev->_page[page + yy]._segs[seg], image, 24);
    }
    seg += 24;
  }
}

void i2c_oled_display_rotate_text(i2c_oled_t *dev, int seg, const char *text,
                                  int text_len, bool invert) {
  int _text_len = text_len;
  if (_text_len > 8)
    _text_len = 8;
  uint8_t image[8];
  int _page = dev->_pages - 1;
  for (uint8_t i = 0; i < _text_len; i++) {
    memcpy(image, font8x8_basic_tr[(uint8_t)text[i]], 8);
    i2c_oled_rotate_image(image, dev->_flip);
    if (invert)
      i2c_oled_invert(image, 8);
    i2c_oled_display_image(dev, _page, seg, image, 8);
    _page--;
    if (_page < 0)
      return;
  }
}

/* ═══════════════════════════════════════════════════════════
 *  Clear
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_clear_screen(i2c_oled_t *dev, bool invert) {
  char space[16];
  memset(space, 0x00, sizeof(space));
  for (int page = 0; page < dev->_pages; page++) {
    i2c_oled_display_text(dev, page, space, sizeof(space), invert);
  }
}

void i2c_oled_clear_line(i2c_oled_t *dev, int page, bool invert) {
  char space[16];
  memset(space, 0x00, sizeof(space));
  i2c_oled_display_text(dev, page, space, sizeof(space), invert);
}

/* ═══════════════════════════════════════════════════════════
 *  Contrast
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_contrast(i2c_oled_t *dev, int contrast) {
  (void)dev;
  uint8_t cmds[2] = {0x81, (uint8_t)contrast};
  i2c_oled_send_cmd(cmds, 2);
}

/* ═══════════════════════════════════════════════════════════
 *  Software scroll
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_software_scroll(i2c_oled_t *dev, int start, int end) {
  ESP_LOGD(TAG, "software_scroll start=%d end=%d _pages=%d", start, end,
           dev->_pages);
  if (start < 0 || end < 0) {
    dev->_scEnable = false;
  } else if (start >= dev->_pages || end >= dev->_pages) {
    dev->_scEnable = false;
  } else {
    dev->_scEnable = true;
    dev->_scStart = start;
    dev->_scEnd = end;
    dev->_scDirection = 1;
    if (start > end)
      dev->_scDirection = -1;
  }
}

void i2c_oled_scroll_text(i2c_oled_t *dev, const char *text, int text_len,
                          bool invert) {
  if (!dev->_scEnable)
    return;

  int srcIndex = dev->_scEnd - dev->_scDirection;
  while (1) {
    int dstIndex = srcIndex + dev->_scDirection;
    for (int seg = 0; seg < dev->_width; seg++) {
      dev->_page[dstIndex]._segs[seg] = dev->_page[srcIndex]._segs[seg];
    }
    i2c_oled_display_page(dev, dstIndex, 0, dev->_page[dstIndex]._segs,
                          sizeof(dev->_page[dstIndex]._segs));
    if (srcIndex == dev->_scStart)
      break;
    srcIndex -= dev->_scDirection;
  }

  int _text_len = text_len;
  if (_text_len > 16)
    _text_len = 16;
  i2c_oled_display_text(dev, srcIndex, text, _text_len, invert);
}

void i2c_oled_scroll_clear(i2c_oled_t *dev) {
  if (!dev->_scEnable)
    return;

  int srcIndex = dev->_scEnd - dev->_scDirection;
  while (1) {
    int dstIndex = srcIndex + dev->_scDirection;
    i2c_oled_clear_line(dev, dstIndex, false);
    if (dstIndex == dev->_scStart)
      break;
    srcIndex -= dev->_scDirection;
  }
}

/* ═══════════════════════════════════════════════════════════
 *  Wrap-around scrolling (pixel & page level)
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_wrap_arround(i2c_oled_t *dev, oled_scroll_type_t scroll,
                           int start, int end, int8_t delay) {
  if (scroll == SCROLL_RIGHT) {
    int _start = start;
    int _end = end;
    if (_end >= dev->_pages)
      _end = dev->_pages - 1;
    uint8_t wk;
    for (int page = _start; page <= _end; page++) {
      wk = dev->_page[page]._segs[127];
      for (int seg = 127; seg > 0; seg--) {
        dev->_page[page]._segs[seg] = dev->_page[page]._segs[seg - 1];
      }
      dev->_page[page]._segs[0] = wk;
    }

  } else if (scroll == SCROLL_LEFT) {
    int _start = start;
    int _end = end;
    if (_end >= dev->_pages)
      _end = dev->_pages - 1;
    uint8_t wk;
    for (int page = _start; page <= _end; page++) {
      wk = dev->_page[page]._segs[0];
      for (int seg = 0; seg < 127; seg++) {
        dev->_page[page]._segs[seg] = dev->_page[page]._segs[seg + 1];
      }
      dev->_page[page]._segs[127] = wk;
    }

  } else if (scroll == SCROLL_UP) {
    int _start = start;
    int _end = end;
    if (_end >= dev->_width)
      _end = dev->_width - 1;
    uint8_t wk0, wk1, wk2;
    uint8_t save[128];
    for (int seg = 0; seg < 128; seg++) {
      save[seg] = dev->_page[0]._segs[seg];
    }
    for (int page = 0; page < dev->_pages - 1; page++) {
      for (int seg = _start; seg <= _end; seg++) {
        wk0 = dev->_page[page]._segs[seg];
        wk1 = dev->_page[page + 1]._segs[seg];
        if (dev->_flip)
          wk0 = i2c_oled_rotate_byte(wk0);
        if (dev->_flip)
          wk1 = i2c_oled_rotate_byte(wk1);
        wk0 = wk0 >> 1;
        wk1 = wk1 & 0x01;
        wk1 = wk1 << 7;
        wk2 = wk0 | wk1;
        if (dev->_flip)
          wk2 = i2c_oled_rotate_byte(wk2);
        dev->_page[page]._segs[seg] = wk2;
      }
    }
    int pages = dev->_pages - 1;
    for (int seg = _start; seg <= _end; seg++) {
      wk0 = dev->_page[pages]._segs[seg];
      wk1 = save[seg];
      if (dev->_flip)
        wk0 = i2c_oled_rotate_byte(wk0);
      if (dev->_flip)
        wk1 = i2c_oled_rotate_byte(wk1);
      wk0 = wk0 >> 1;
      wk1 = wk1 & 0x01;
      wk1 = wk1 << 7;
      wk2 = wk0 | wk1;
      if (dev->_flip)
        wk2 = i2c_oled_rotate_byte(wk2);
      dev->_page[pages]._segs[seg] = wk2;
    }

  } else if (scroll == SCROLL_DOWN) {
    int _start = start;
    int _end = end;
    if (_end >= dev->_width)
      _end = dev->_width - 1;
    uint8_t wk0, wk1, wk2;
    uint8_t save[128];
    int pages = dev->_pages - 1;
    for (int seg = 0; seg < 128; seg++) {
      save[seg] = dev->_page[pages]._segs[seg];
    }
    for (int page = pages; page > 0; page--) {
      for (int seg = _start; seg <= _end; seg++) {
        wk0 = dev->_page[page]._segs[seg];
        wk1 = dev->_page[page - 1]._segs[seg];
        if (dev->_flip)
          wk0 = i2c_oled_rotate_byte(wk0);
        if (dev->_flip)
          wk1 = i2c_oled_rotate_byte(wk1);
        wk0 = wk0 << 1;
        wk1 = wk1 & 0x80;
        wk1 = wk1 >> 7;
        wk2 = wk0 | wk1;
        if (dev->_flip)
          wk2 = i2c_oled_rotate_byte(wk2);
        dev->_page[page]._segs[seg] = wk2;
      }
    }
    for (int seg = _start; seg <= _end; seg++) {
      wk0 = dev->_page[0]._segs[seg];
      wk1 = save[seg];
      if (dev->_flip)
        wk0 = i2c_oled_rotate_byte(wk0);
      if (dev->_flip)
        wk1 = i2c_oled_rotate_byte(wk1);
      wk0 = wk0 << 1;
      wk1 = wk1 & 0x80;
      wk1 = wk1 >> 7;
      wk2 = wk0 | wk1;
      if (dev->_flip)
        wk2 = i2c_oled_rotate_byte(wk2);
      dev->_page[0]._segs[seg] = wk2;
    }

  } else if (scroll == PAGE_SCROLL_DOWN) {
    uint8_t save[128];
    for (int seg = 0; seg < 128; seg++) {
      save[seg] = dev->_page[dev->_pages - 1]._segs[seg];
    }
    for (int page = dev->_pages - 1; page > 0; page--) {
      for (int seg = 0; seg < 128; seg++) {
        dev->_page[page]._segs[seg] = dev->_page[page - 1]._segs[seg];
      }
    }
    for (int seg = 0; seg < 128; seg++) {
      dev->_page[0]._segs[seg] = save[seg];
    }

  } else if (scroll == PAGE_SCROLL_UP) {
    uint8_t save[128];
    for (int seg = 0; seg < 128; seg++) {
      save[seg] = dev->_page[0]._segs[seg];
    }
    for (int page = 0; page < dev->_pages - 1; page++) {
      for (int seg = 0; seg < 128; seg++) {
        dev->_page[page]._segs[seg] = dev->_page[page + 1]._segs[seg];
      }
    }
    for (int seg = 0; seg < 128; seg++) {
      dev->_page[dev->_pages - 1]._segs[seg] = save[seg];
    }
  }

  if (delay >= 0) {
    for (int page = 0; page < dev->_pages; page++) {
      i2c_oled_display_page(dev, page, 0, dev->_page[page]._segs, 128);
      if (delay)
        vTaskDelay(delay);
    }
  }
}

/* ═══════════════════════════════════════════════════════════
 *  Bitmap rendering
 * ═══════════════════════════════════════════════════════════ */

static void _i2c_oled_bitmaps(i2c_oled_t *dev, int xpos, int ypos,
                              const uint8_t *bitmap, int width, int height,
                              bool invert) {
  if ((width % 8) != 0) {
    ESP_LOGE(TAG, "width must be a multiple of 8");
    return;
  }
  int _width = width / 8;
  uint8_t wk0, wk1, wk2;
  uint8_t page = (ypos / 8);
  uint8_t _seg = xpos;
  uint8_t dstBits = (ypos % 8);
  int offset = 0;

  for (int _height = 0; _height < height; _height++) {
    for (int index = 0; index < _width; index++) {
      for (int srcBits = 7; srcBits >= 0; srcBits--) {
        wk0 = dev->_page[page]._segs[_seg];
        if (dev->_flip)
          wk0 = i2c_oled_rotate_byte(wk0);

        wk1 = bitmap[index + offset];
        if (invert)
          wk1 = ~wk1;

        wk2 = i2c_oled_copy_bit(wk1, srcBits, wk0, dstBits);
        if (dev->_flip)
          wk2 = i2c_oled_rotate_byte(wk2);

        if (_seg >= 128)
          break;
        if (page >= dev->_pages)
          break;
        dev->_page[page]._segs[_seg] = wk2;
        _seg++;
      }
    }
    offset += _width;
    dstBits++;
    _seg = xpos;
    if (dstBits == 8) {
      page++;
      dstBits = 0;
    }
  }
}

void i2c_oled_bitmaps(i2c_oled_t *dev, int xpos, int ypos,
                      const uint8_t *bitmap, int width, int height,
                      bool invert) {
  _i2c_oled_bitmaps(dev, xpos, ypos, bitmap, width, height, invert);

  int start_page = ypos / 8;
  int end_page = (ypos + height - 1) / 8;
  int start_seg = xpos;
  int end_seg = xpos + width - 1;

  for (int page = start_page; page <= end_page; page++) {
    int seg_start = (page == start_page) ? start_seg : 0;
    int seg_end = (page == end_page) ? end_seg : 127;
    int seg_width = seg_end - seg_start + 1;
    i2c_oled_display_image(dev, page, seg_start,
                           &dev->_page[page]._segs[seg_start], seg_width);
  }
}

/* ═══════════════════════════════════════════════════════════
 *  Drawing primitives (to internal buffer only)
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_pixel(i2c_oled_t *dev, int xpos, int ypos, bool invert) {
  uint8_t _page = (ypos / 8);
  uint8_t _bits = (ypos % 8);
  uint8_t _seg = xpos;
  uint8_t wk0 = dev->_page[_page]._segs[_seg];
  uint8_t wk1 = 1 << _bits;
  if (invert) {
    wk0 = wk0 & ~wk1;
  } else {
    wk0 = wk0 | wk1;
  }
  if (dev->_flip)
    wk0 = i2c_oled_rotate_byte(wk0);
  dev->_page[_page]._segs[_seg] = wk0;
}

void i2c_oled_line(i2c_oled_t *dev, int x1, int y1, int x2, int y2,
                   bool invert) {
  int i, dx, dy, sx, sy, E;

  dx = (x2 > x1) ? x2 - x1 : x1 - x2;
  dy = (y2 > y1) ? y2 - y1 : y1 - y2;
  sx = (x2 > x1) ? 1 : -1;
  sy = (y2 > y1) ? 1 : -1;

  if (dx > dy) {
    E = -dx;
    for (i = 0; i <= dx; i++) {
      i2c_oled_pixel(dev, x1, y1, invert);
      x1 += sx;
      E += 2 * dy;
      if (E >= 0) {
        y1 += sy;
        E -= 2 * dx;
      }
    }
  } else {
    E = -dy;
    for (i = 0; i <= dy; i++) {
      i2c_oled_pixel(dev, x1, y1, invert);
      y1 += sy;
      E += 2 * dx;
      if (E >= 0) {
        x1 += sx;
        E -= 2 * dy;
      }
    }
  }
}

void i2c_oled_circle(i2c_oled_t *dev, int x0, int y0, int r, unsigned int opt,
                     bool invert) {
  int x = 0, y = -r, err = 2 - 2 * r, old_err;
  do {
    if (opt & OLED_DRAW_UPPER_LEFT)
      i2c_oled_pixel(dev, x0 - x, y0 + y, invert);
    if (opt & OLED_DRAW_UPPER_RIGHT)
      i2c_oled_pixel(dev, x0 - y, y0 - x, invert);
    if (opt & OLED_DRAW_LOWER_RIGHT)
      i2c_oled_pixel(dev, x0 + x, y0 - y, invert);
    if (opt & OLED_DRAW_LOWER_LEFT)
      i2c_oled_pixel(dev, x0 + y, y0 + x, invert);
    if ((old_err = err) <= x)
      err += ++x * 2 + 1;
    if (old_err > y || err > x)
      err += ++y * 2 + 1;
  } while (y < 0);
}

void i2c_oled_disc(i2c_oled_t *dev, int x0, int y0, int r, unsigned int opt,
                   bool invert) {
  int x = 0, y = -r, err = 2 - 2 * r, old_err;
  int ChangeX = 1;
  do {
    if (ChangeX) {
      if (opt & OLED_DRAW_LOWER_LEFT)
        i2c_oled_line(dev, x0 - x, y0 - y, x0 - x, y0, invert);
      if (opt & OLED_DRAW_UPPER_LEFT)
        i2c_oled_line(dev, x0 - x, y0, x0 - x, y0 + y, invert);
      if (opt & OLED_DRAW_LOWER_RIGHT)
        i2c_oled_line(dev, x0 + x, y0 - y, x0 + x, y0, invert);
      if (opt & OLED_DRAW_UPPER_RIGHT)
        i2c_oled_line(dev, x0 + x, y0, x0 + x, y0 + y, invert);
    }
    ChangeX = (old_err = err) <= x;
    if (ChangeX)
      err += ++x * 2 + 1;
    if (old_err > y || err > x)
      err += ++y * 2 + 1;
  } while (y <= 0);
}

void i2c_oled_cursor(i2c_oled_t *dev, int x0, int y0, int r, bool invert) {
  i2c_oled_line(dev, x0 - r, y0, x0 + r, y0, invert);
  i2c_oled_line(dev, x0, y0 - r, x0, y0 + r, invert);
}

/* ═══════════════════════════════════════════════════════════
 *  Effects
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_fadeout(i2c_oled_t *dev) {
  uint8_t image[1];
  for (int page = 0; page < dev->_pages; page++) {
    image[0] = 0xFF;
    for (int line = 0; line < 8; line++) {
      if (dev->_flip) {
        image[0] = image[0] >> 1;
      } else {
        image[0] = image[0] << 1;
      }
      for (int seg = 0; seg < 128; seg++) {
        i2c_oled_display_page(dev, page, seg, image, 1);
        dev->_page[page]._segs[seg] = image[0];
      }
    }
  }
}

/* ═══════════════════════════════════════════════════════════
 *  Raw GIF-frame bitmap (for gif_player compatibility)
 *  Converts row-major 1-bit bitmap → column-major page buffer
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_draw_bitmap_raw(uint16_t x, uint16_t y, const uint8_t *bitmap,
                              uint16_t w, uint16_t h) {
  (void)x;
  (void)y;
  memset(display_buffer, 0, sizeof(display_buffer));
  for (int r = 0; r < 64; r++) {
    for (int c = 0; c < 128; c++) {
      int byte_idx = (r * 128 + c) / 8;
      int bit_idx = 7 - ((r * 128 + c) % 8);
      if (bitmap[byte_idx] & (1 << bit_idx)) {
        int page = r / 8;
        int v_bit = r % 8;
        display_buffer[page * 128 + c] |= (1 << v_bit);
      }
    }
  }
}

void i2c_oled_refresh(void) {
  /* Set column range 0..127 */
  uint8_t cmd_col[] = {0x21, 0, 127};
  i2c_oled_send_cmd(cmd_col, sizeof(cmd_col));

  /* Set page range 0..7 */
  uint8_t cmd_page[] = {0x22, 0, 7};
  i2c_oled_send_cmd(cmd_page, sizeof(cmd_page));

  /* Temporarily switch to horizontal addressing for bulk write */
  uint8_t cmd_mode[] = {0x20, 0x00};
  i2c_oled_send_cmd(cmd_mode, sizeof(cmd_mode));

  /* Push entire 1024-byte buffer */
  i2c_oled_send_data(display_buffer, 1024);

  /* Restore page addressing for other functions */
  uint8_t cmd_restore[] = {0x20, 0x02};
  i2c_oled_send_cmd(cmd_restore, sizeof(cmd_restore));
}

/* ═══════════════════════════════════════════════════════════
 *  Debug
 * ═══════════════════════════════════════════════════════════ */

void i2c_oled_dump(i2c_oled_t dev) {
  printf("_width=%d\n", dev._width);
  printf("_height=%d\n", dev._height);
  printf("_pages=%d\n", dev._pages);
}

void i2c_oled_dump_page(i2c_oled_t *dev, int page, int seg) {
  ESP_LOGI(TAG, "dev->_page[%d]._segs[%d]=%02x", page, seg,
           dev->_page[page]._segs[seg]);
}