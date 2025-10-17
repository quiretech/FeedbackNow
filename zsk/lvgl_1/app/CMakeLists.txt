cmake_minimum_required(VERSION 3.20.0)

# Add the PN5180 driver module before finding Zephyr
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../ssd1683)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lvgl_1)

# Add custom driver module
# add_subdirectory(../ssd1683 ssd1683_build)

# Generate CFB fonts from TTF files
set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
include(${ZEPHYR_BASE}/cmake/cfb.cmake NO_POLICY_SCOPE)
# Generate FreeSans font (12x16)
generate_cfb_font_for_target(app 
    src/fonts/FreeSans.ttf 
    ${gen_dir}/cfb_mono_FreeSans.h 
    12 16 
    -s 12 
    --first 32 
    --last 126 
    --name cfb_custom_1
)

# Generate FreeSansBold font (12x16)
generate_cfb_font_for_target(app 
    src/fonts/FreeSansBold.ttf 
    ${gen_dir}/cfb_mono_FreeSansBold.h 
    12 16 
    -s 12 
    --first 32 
    --last 126 
    --name cfb_custom_2
)

# Add all the C files you want built for the app here.
# Keep source files in src/ and list them below.
target_sources(app PRIVATE
    src/main.c
    # src/fonts/roboto_20.c
    # src/fonts/roboto_28.c
    # src/fonts/roboto_32.c
    # src/fonts/roboto_36.c
    # src/fonts/roboto_42.c
    # src/fonts/roboto_bold_42.c
    src/fonts/cfbv_1016.h
    ${gen_dir}/cfb_mono_FreeSans.h
    ${gen_dir}/cfb_mono_FreeSansBold.h
    src/test_partial.c
    src/test_partial.h
)

# Include directories
target_include_directories(app PRIVATE
    ../ssd1683/include
    ${gen_dir}
)

