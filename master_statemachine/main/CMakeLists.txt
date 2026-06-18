idf_component_register(SRCS "master_statemachine.c"
                    INCLUDE_DIRS "."
                    REQUIRES touch_driver i2c_oled Reaction_files)
