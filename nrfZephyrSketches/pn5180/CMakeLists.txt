cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(pn5180)

# Add source files
target_sources(app PRIVATE
    src/main.c
    src/pn5180.c
    include/pn5180.h
)

# Add include directory for headers
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
