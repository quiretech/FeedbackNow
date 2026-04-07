cmake_minimum_required(VERSION 3.20.0)

# Add the PN5180 driver module before finding Zephyr
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../pn5180)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(pn5180_nfc_app)

# Add source files
target_sources(app PRIVATE
    src/main.c
    src/power_ctrl.c
)

# Add include directories
target_include_directories(app PRIVATE
    ../pn5180/include
)