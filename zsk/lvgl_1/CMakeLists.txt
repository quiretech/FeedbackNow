# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lvgl_1)

# Add all the C files you want built for the app here.
# Keep source files in src/ and list them below.
target_sources(app PRIVATE
    src/main.c
    src/ssd1683.c
    src/ssd1683.h
    src/image_bitmap.h
    src/image_bitmap.c



)
