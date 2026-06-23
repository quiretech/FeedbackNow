# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Match onboarding_config.h DEVICE_HW_VARIANT → Kconfig fragment, devicetree, modules.
file(READ "${APP_DIR}/include/onboarding_config.h" ONBOARDING_CONFIG_H_CONTENT)
if(ONBOARDING_CONFIG_H_CONTENT MATCHES "#define DEVICE_HW_VARIANT FLEXBOX_PLUS")
  set(FB_DEVICE_HW_VARIANT "FLEXBOX_PLUS")
elseif(ONBOARDING_CONFIG_H_CONTENT MATCHES "#define DEVICE_HW_VARIANT FLEXBOX")
  set(FB_DEVICE_HW_VARIANT "FLEXBOX")
else()
  message(FATAL_ERROR
    "onboarding_config.h: DEVICE_HW_VARIANT must be FLEXBOX or FLEXBOX_PLUS")
endif()

list(APPEND CONF_FILE "${APP_DIR}/prj.conf")
if(FB_DEVICE_HW_VARIANT STREQUAL "FLEXBOX_PLUS")
  list(APPEND CONF_FILE "${APP_DIR}/prj_flexbox_plus.conf")
  list(APPEND ZEPHYR_EXTRA_MODULES
    ${APP_DIR}/pn5180
    ${APP_DIR}/ssd1683
  )
  list(APPEND DTC_OVERLAY_FILE
    ${APP_DIR}/boards/nrf52840dk_nrf52840.overlay
    ${APP_DIR}/boards/nrf52840dk_nrf52840_flexbox_plus.overlay
  )
else()
  list(APPEND CONF_FILE "${APP_DIR}/prj_flexbox.conf")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(feedbacknow1.2)

message(STATUS "FlexBox DEVICE_HW_VARIANT: ${FB_DEVICE_HW_VARIANT}")

target_include_directories(app PRIVATE
  ${APP_DIR}/include
)

set(APP_SOURCES
  src/main.c
  src/boot_info.c
  src/hw_variant_guard.c
  src/lora_app.c
  src/lora_link_stats.c
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
  src/counter_sync.c
  src/downlink_dispatch.c
  src/display_manager.c
  src/nfc_service.c
  src/app_logic.c
  src/housekeeping.c
  src/adc.c
)

if(FB_DEVICE_HW_VARIANT STREQUAL "FLEXBOX_PLUS")
  target_include_directories(app PRIVATE
    ${APP_DIR}/assets/fonts
    ${APP_DIR}/assets/display_screens
  )
  list(APPEND APP_SOURCES
    assets/fonts/roboto_20.c
    assets/fonts/roboto_32.c
    assets/fonts/roboto_28.c
    assets/fonts/roboto_36.c
    assets/fonts/roboto_bold_36.c
    assets/fonts/roboto_bold_42.c
    assets/display_screens/boot_screen.c
    assets/display_screens/thanks_en.c
    assets/display_screens/thanks_fr.c
    assets/display_screens/thanks_de.c
    assets/display_screens/cleaning_en.c
    assets/display_screens/cleaning_fr.c
    assets/display_screens/cleaning_de.c
  )
endif()

target_sources(app PRIVATE ${APP_SOURCES})
