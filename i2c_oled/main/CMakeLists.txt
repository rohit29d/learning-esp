idf_component_register(SRCS "i2c_oled_main.c" "oled_driver.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES driver esp_driver_gpio)
