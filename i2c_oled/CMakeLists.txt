cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    # Trim the build to this example's main component and its dependencies.
    idf_build_set_property(MINIMAL_BUILD ON)
    project(i2c_oled)
else()
    # This folder is a standalone ESP-IDF example copied into another
    # project's components directory. When the parent project scans it as a
    # component, keep it inert so the example's app_main is not linked into the
    # parent firmware.
    idf_component_register()
endif()
