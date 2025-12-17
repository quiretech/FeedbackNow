# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(eeprom)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/power_ctrl.c)
target_include_directories(app PRIVATE src)

# find_package(PowerCtrl REQUIRED)
# target_link_libraries(app PRIVATE PowerCtrl)