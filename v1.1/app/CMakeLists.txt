# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# 1. Modules should be defined BEFORE find_package(Zephyr)
# Using Zephyr's native module system is cleaner than appending to a list
set(ZEPHYR_EXTRA_MODULES 
  ${CMAKE_CURRENT_SOURCE_DIR}/pn5180
  ${CMAKE_CURRENT_SOURCE_DIR}/ssd1683
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(feedbacknow1.2)

# 2. Add application include directories
# PRIVATE ensures these headers don't leak to other modules
target_include_directories(app PRIVATE 
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/display_screens
)

# 3. Use a file list for cleaner source management
# This makes it much easier to read and maintain
target_sources(app PRIVATE
  src/main.c
  src/lora_app.c
  src/lora_thread.c
  src/devnonce_store.c
  src/join_state_store.c
  src/payload_gen.c
  src/power_ctrl.c
  src/rail_manager.c
  src/buttons.c
  src/button_thread.c
  src/leds.c
  src/led_manager.c
  src/time_sync.c
  src/eeprom_probe.c
  src/button_counter_store.c
  src/last_cleaned_store.c
  src/tz_offset_store.c
  src/rtc.c
  src/smf_system_mode.c
  src/display_manager.c
  src/nfc_service.c
  src/app_logic.c
  src/housekeeping.c
  src/adc.c
  assets/fonts/roboto_20.c
  assets/fonts/roboto_32.c
  assets/fonts/roboto_28.c
  assets/fonts/roboto_36.c
  assets/fonts/roboto_bold_42.c
  assets/display_screens/bootLogo.c
  assets/display_screens/AckEng.c
  assets/display_screens/CleaningScreen.c
)