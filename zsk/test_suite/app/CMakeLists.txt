# SPDX-License-Identifier: Apache-2.0
#
# FlexBox factory self-test image — minimal app sources (no LoRaWAN/SMF/NFC
# service threads; main.c drives peripherals directly).

cmake_minimum_required(VERSION 3.20.0)

set(ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/pn5180
  ${CMAKE_CURRENT_SOURCE_DIR}/ssd1683
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flexbox_test_suite)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
)

target_sources(app PRIVATE
  src/main.c
  src/power_ctrl.c
  src/rail_manager.c
  src/buttons.c
  src/leds.c
  src/rtc.c
  src/display_manager.c
  src/last_cleaned_store.c
  assets/fonts/roboto_20.c
  assets/fonts/roboto_28.c
  assets/fonts/roboto_36.c
  assets/fonts/roboto_bold_42.c
)
