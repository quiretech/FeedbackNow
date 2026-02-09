# FlexBox v1.2 app — Zephyr
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fb_now)

target_sources(app PRIVATE
  src/main.c
  src/buttons.c
  src/button_thread.c
)
target_include_directories(app PRIVATE include)
