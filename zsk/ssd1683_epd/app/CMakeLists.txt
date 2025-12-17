cmake_minimum_required(VERSION 3.20.0)


list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../ssd1683)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ssd1683_epd)


# Add all the C files you want built for the app here.
# Keep source files in src/ and list them below.
target_sources(app PRIVATE
    src/main.c
    src/power_ctrl.c
    fonts/roboto_28.c
    fonts/roboto_36.c
    fonts/roboto_bold_42.c
)

# Include directories
target_include_directories(app PRIVATE
    ../ssd1683/include
    ${gen_dir}
    fonts
    src
)