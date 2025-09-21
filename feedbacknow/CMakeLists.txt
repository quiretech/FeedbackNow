# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Add the PN5180 driver module before finding Zephyr
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/pn5180)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(feedbacknow)

zephyr_include_directories(include)
zephyr_include_directories(pn5180/include)
FILE(GLOB app_sources src/*.c )
target_sources(app PRIVATE ${app_sources})

# Ensure new architecture files are included
target_sources(app PRIVATE 
    src/state_manager.c
    src/led_manager.c
    src/lora_manager.c
    src/nfc_manager.c
    src/system_monitor.c
)