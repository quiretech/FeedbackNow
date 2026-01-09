# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lorawan)

# Explicit sources to ensure sysbuild picks up new files
target_sources(app PRIVATE
  src/main.c
  src/lora_app.c
  src/lora_thread.c
  src/devnonce_store.c
  src/payload_gen.c
  src/power_ctrl.c
  src/buttons.c
  src/button_thread.c
  src/leds.c
  src/led_manager.c
  src/time_sync.c
  src/eeprom_probe.c
  src/button_counter_store.c
  src/rtc.c
)
