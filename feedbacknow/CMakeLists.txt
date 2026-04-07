# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Add custom driver modules before finding Zephyr
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/pn5180)
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/ssd1683)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(feedbacknow)

zephyr_include_directories(include)
zephyr_include_directories(pn5180/include)
zephyr_include_directories(ssd1683/include)
zephyr_include_directories(assets/fonts)

FILE(GLOB app_sources src/*.c )
FILE(GLOB font_sources assets/fonts/*.c)
target_sources(app PRIVATE ${app_sources})
target_sources(app PRIVATE ${font_sources})

# Ensure new architecture files are included
target_sources(app PRIVATE 
    src/state_manager.c
    src/led_manager.c
    src/lora_manager.c
    src/nfc_manager.c
    src/system_monitor.c
    src/power_rail_mgr.c
)