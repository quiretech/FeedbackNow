# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Add the PN5180 driver module before finding Zephyr
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/pn5180)

# Add the SSD1683 e-paper display driver module
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/ssd1683)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(demo)

# Add all the C files you want built for the app here.
# Keep source files in src/ and list them below.
target_sources(app PRIVATE
    src/main.c
    src/buttons.c
    src/buttons.h
    src/leds.c
    src/leds.h
    assets/fonts/roboto_20.c
    assets/fonts/roboto_28.c
    assets/fonts/roboto_bold_42.c
    assets/fonts/roboto_semibold_25.c
)
