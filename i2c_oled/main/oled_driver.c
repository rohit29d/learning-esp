#include "oled_driver.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_bit_defs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "oled_driver";

#define OLED_I2C_CLOCK_HZ     (CONFIG_OLED_DRIVER_I2C_CLOCK_KHZ * 1000)
#define OLED_FRAMEBUFFER_SIZE (OLED_DRIVER_WIDTH * OLED_DRIVER_HEIGHT / 8)
#define OLED_I2C_TIMEOUT_MS   100
#define OLED_CMD_CONTROL      0x00
#define OLED_DATA_CONTROL     0x40
#define OLED_DATA_CHUNK_SIZE  16

static bool s_initialized;
static uint8_t s_oled_buffer[OLED_FRAMEBUFFER_SIZE];

static const uint8_t s_font_5x7[][5] = {
    [' ' - ' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['!' - ' '] = {0x00, 0x00, 0x5F, 0x00, 0x00},
    ['"' - ' '] = {0x00, 0x07, 0x00, 0x07, 0x00},
    ['#' - ' '] = {0x14, 0x7F, 0x14, 0x7F, 0x14},
    ['$' - ' '] = {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    ['%' - ' '] = {0x23, 0x13, 0x08, 0x64, 0x62},
    ['&' - ' '] = {0x36, 0x49, 0x55, 0x22, 0x50},
    ['\'' - ' '] = {0x00, 0x05, 0x03, 0x00, 0x00},
    ['(' - ' '] = {0x00, 0x1C, 0x22, 0x41, 0x00},
    [')' - ' '] = {0x00, 0x41, 0x22, 0x1C, 0x00},
    ['*' - ' '] = {0x14, 0x08, 0x3E, 0x08, 0x14},
    ['+' - ' '] = {0x08, 0x08, 0x3E, 0x08, 0x08},
    [',' - ' '] = {0x00, 0x50, 0x30, 0x00, 0x00},
    ['-' - ' '] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['.' - ' '] = {0x00, 0x60, 0x60, 0x00, 0x00},
    ['/' - ' '] = {0x20, 0x10, 0x08, 0x04, 0x02},
    ['0' - ' '] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1' - ' '] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2' - ' '] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3' - ' '] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4' - ' '] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5' - ' '] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6' - ' '] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7' - ' '] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8' - ' '] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9' - ' '] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    [':' - ' '] = {0x00, 0x36, 0x36, 0x00, 0x00},
    [';' - ' '] = {0x00, 0x56, 0x36, 0x00, 0x00},
    ['<' - ' '] = {0x08, 0x14, 0x22, 0x41, 0x00},
    ['=' - ' '] = {0x14, 0x14, 0x14, 0x14, 0x14},
    ['>' - ' '] = {0x00, 0x41, 0x22, 0x14, 0x08},
    ['?' - ' '] = {0x02, 0x01, 0x51, 0x09, 0x06},
    ['@' - ' '] = {0x32, 0x49, 0x79, 0x41, 0x3E},
    ['A' - ' '] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
    ['B' - ' '] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['D' - ' '] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E' - ' '] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G' - ' '] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H' - ' '] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I' - ' '] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['J' - ' '] = {0x20, 0x40, 0x41, 0x3F, 0x01},
    ['K' - ' '] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L' - ' '] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M' - ' '] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['N' - ' '] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q' - ' '] = {0x3E, 0x41, 0x51, 0x21, 0x5E},
    ['R' - ' '] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S' - ' '] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T' - ' '] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U' - ' '] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V' - ' '] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W' - ' '] = {0x3F, 0x40, 0x38, 0x40, 0x3F},
    ['X' - ' '] = {0x63, 0x14, 0x08, 0x14, 0x63},
    ['Y' - ' '] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z' - ' '] = {0x61, 0x51, 0x49, 0x45, 0x43},
};

static esp_err_t oled_write_bytes(uint8_t control_byte, const uint8_t *data, size_t data_len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        ESP_LOGE(TAG, "failed to create I2C command");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = i2c_master_start(cmd);
    if (ret == ESP_OK) {
        ret = i2c_master_write_byte(cmd, (CONFIG_OLED_DRIVER_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    }
    if (ret == ESP_OK) {
        ret = i2c_master_write_byte(cmd, control_byte, true);
    }
    if (ret == ESP_OK && data_len > 0) {
        ret = i2c_master_write(cmd, (uint8_t *)data, data_len, true);
    }
    if (ret == ESP_OK) {
        ret = i2c_master_stop(cmd);
    }
    if (ret == ESP_OK) {
        ret = i2c_master_cmd_begin(CONFIG_OLED_DRIVER_I2C_PORT, cmd, pdMS_TO_TICKS(OLED_I2C_TIMEOUT_MS));
    }

    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t oled_write_command(uint8_t command)
{
    return oled_write_bytes(OLED_CMD_CONTROL, &command, 1);
}

static esp_err_t oled_write_command_list(const uint8_t *commands, size_t command_count)
{
    return oled_write_bytes(OLED_CMD_CONTROL, commands, command_count);
}

esp_err_t oled_driver_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initialize I2C bus");
    if (CONFIG_OLED_DRIVER_PIN_RST >= 0) {
        gpio_config_t reset_config = {
            .pin_bit_mask = 1ULL << CONFIG_OLED_DRIVER_PIN_RST,
            .mode = GPIO_MODE_OUTPUT,
        };
        esp_err_t ret = gpio_config(&reset_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to configure reset GPIO");
            return ret;
        }
        gpio_set_level(CONFIG_OLED_DRIVER_PIN_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(CONFIG_OLED_DRIVER_PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_OLED_DRIVER_PIN_SDA,
        .scl_io_num = CONFIG_OLED_DRIVER_PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_CLOCK_HZ,
    };
    esp_err_t ret = i2c_param_config(CONFIG_OLED_DRIVER_I2C_PORT, &i2c_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure I2C");
        return ret;
    }

    ret = i2c_driver_install(CONFIG_OLED_DRIVER_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to install I2C driver");
        return ret;
    }

    ESP_LOGI(TAG, "Initialize SSD1306 OLED");
    const uint8_t init_commands[] = {
        0xAE,                         // Display off
        0x20, 0x00,                   // Horizontal addressing mode
        0xB0,                         // Page start address
        0xC8,                         // COM output scan direction remapped
        0x00,                         // Low column address
        0x10,                         // High column address
        0x40,                         // Display start line
        0x81, 0x7F,                   // Contrast
        0xA1,                         // Segment remap
        0xA6,                         // Normal display
        0xA8, OLED_DRIVER_HEIGHT - 1, // Multiplex ratio
        0xA4,                         // Resume RAM display
        0xD3, 0x00,                   // Display offset
        0xD5, 0x80,                   // Display clock
        0xD9, 0xF1,                   // Pre-charge period
        0xDA, OLED_DRIVER_HEIGHT == 64 ? 0x12 : 0x02,
        0xDB, 0x40,                   // VCOM deselect level
        0x8D, 0x14,                   // Charge pump enable
#if CONFIG_OLED_DRIVER_INVERT_COLOR
        0xA7,                         // Inverted display
#else
        0xA6,                         // Normal display
#endif
        0xAF,                         // Display on
    };
    ret = oled_write_command_list(init_commands, sizeof(init_commands));
    if (ret != ESP_OK) {
        (void)i2c_driver_delete(CONFIG_OLED_DRIVER_I2C_PORT);
        return ret;
    }

    s_initialized = true;
    oled_driver_clear();
    return oled_driver_flush();
}

esp_err_t oled_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    (void)oled_write_command(0xAE);
    s_initialized = false;
    return i2c_driver_delete(CONFIG_OLED_DRIVER_I2C_PORT);
}

esp_err_t oled_driver_flush(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "OLED is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t address_commands[] = {
        0x21, 0x00, OLED_DRIVER_WIDTH - 1,
        0x22, 0x00, (OLED_DRIVER_HEIGHT / 8) - 1,
    };
    esp_err_t ret = oled_write_command_list(address_commands, sizeof(address_commands));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to set SSD1306 address window");
        return ret;
    }

    for (size_t offset = 0; offset < sizeof(s_oled_buffer); offset += OLED_DATA_CHUNK_SIZE) {
        size_t chunk_size = sizeof(s_oled_buffer) - offset;
        if (chunk_size > OLED_DATA_CHUNK_SIZE) {
            chunk_size = OLED_DATA_CHUNK_SIZE;
        }

        ret = oled_write_bytes(OLED_DATA_CONTROL, &s_oled_buffer[offset], chunk_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to write SSD1306 data");
            return ret;
        }
    }

    return ESP_OK;
}

void oled_driver_clear(void)
{
    memset(s_oled_buffer, 0, sizeof(s_oled_buffer));
}

void oled_driver_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_DRIVER_WIDTH || y < 0 || y >= OLED_DRIVER_HEIGHT) {
        return;
    }

    uint8_t *byte = &s_oled_buffer[OLED_DRIVER_WIDTH * (y / 8) + x];
    if (on) {
        *byte |= BIT(y % 8);
    } else {
        *byte &= ~BIT(y % 8);
    }
}

void oled_driver_draw_char(int x, int y, char c)
{
    if (c < ' ' || c > 'Z') {
        c = ' ';
    }

    const uint8_t *glyph = s_font_5x7[c - ' '];
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            oled_driver_set_pixel(x + col, y + row, glyph[col] & BIT(row));
        }
    }
}

void oled_driver_draw_text(int x, int y, const char *text)
{
    if (text == NULL) {
        return;
    }

    while (*text != '\0') {
        oled_driver_draw_char(x, y, *text++);
        x += 6;
    }
}

void oled_driver_draw_frame(void)
{
    for (int x = 0; x < OLED_DRIVER_WIDTH; x++) {
        oled_driver_set_pixel(x, 0, true);
        oled_driver_set_pixel(x, OLED_DRIVER_HEIGHT - 1, true);
    }

    for (int y = 0; y < OLED_DRIVER_HEIGHT; y++) {
        oled_driver_set_pixel(0, y, true);
        oled_driver_set_pixel(OLED_DRIVER_WIDTH - 1, y, true);
    }
}

uint8_t *oled_driver_get_buffer(void)
{
    return s_oled_buffer;
}
