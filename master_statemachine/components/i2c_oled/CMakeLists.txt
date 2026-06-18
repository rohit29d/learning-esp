idf_component_register(
    SRCS "i2c_oled.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos Reaction_files
)
