idf_component_register(
    SRCS "touch_driver.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos esp_timer esp_driver_gpio
)
