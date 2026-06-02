| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- | -------- |

# I2C SSD1306 OLED driver

This project provides a small raw-I2C SSD1306 OLED driver for ESP-IDF. The driver initializes the I2C bus, sends SSD1306 commands directly, keeps a 1-bit framebuffer, and exposes basic drawing helpers.

It does not use `esp_lcd`, managed components, or the ESP Component Registry. The only ESP-IDF dependency is the basic `driver/i2c.h` and GPIO/FreeRTOS support that ship with ESP-IDF.

The application entry point only initializes and clears the OLED. Add your application logic after `oled_driver_init()` or call the driver functions from another module.

## Driver API

```c
esp_err_t oled_driver_init(void);
esp_err_t oled_driver_deinit(void);
esp_err_t oled_driver_flush(void);

void oled_driver_clear(void);
void oled_driver_set_pixel(int x, int y, bool on);
void oled_driver_draw_char(int x, int y, char c);
void oled_driver_draw_text(int x, int y, const char *text);
void oled_driver_draw_frame(void);
uint8_t *oled_driver_get_buffer(void);
```

Call `oled_driver_flush()` after changing the framebuffer.

## Hardware

Default ESP32 wiring:

```text
ESP32 GPIO21 -> OLED SDA
ESP32 GPIO22 -> OLED SCL
ESP32 3V3    -> OLED VCC
ESP32 GND    -> OLED GND
```

The default I2C address is `0x3C`.

## Configuration

Run `idf.py menuconfig` and open `OLED Driver Configuration` to change:

* I2C port
* SDA/SCL/reset GPIOs
* OLED I2C address
* I2C clock speed
* OLED width and height
* color inversion

## Build and Flash

```powershell
idf.py -p COM_PORT build flash monitor
```

On boot, the OLED is initialized, cleared, and left ready for your drawing code.
