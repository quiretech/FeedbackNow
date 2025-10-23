
/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 *
 * SSD1683 E-Paper Display Driver - Zephyr Display API Wrapper
 *
 * This file wraps the low-level SSD1683 driver (ssd1683.c) to implement
 * the Zephyr display driver API, allowing integration with LVGL and other
 * graphics frameworks.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#include "ssd1683.h"

LOG_MODULE_REGISTER(ssd1683_display, CONFIG_DISPLAY_LOG_LEVEL);

#define DT_DRV_COMPAT solomon_ssd1683

// ============================================================================
// Zephyr Display Driver API Structure
// ============================================================================

static const struct display_driver_api ssd1683_display_api = {
    .blanking_on = ssd1683_display_blanking_on,
    .blanking_off = ssd1683_display_blanking_off,
    .write = ssd1683_display_write,
    .get_capabilities = ssd1683_display_get_capabilities,
    .set_pixel_format = ssd1683_display_set_pixel_format,
    // Not implemented (return -ENOTSUP by default):
    // .read = NULL,
    // .get_framebuffer = NULL,
    // .set_brightness = NULL,
    // .set_contrast = NULL,
    // .set_orientation = NULL,
};

/**
 * Macro to instantiate the driver for each device tree node
 * with compatible = "solomon,ssd1683"
 *
 * This follows the modern Zephyr node-based approach (like ssd16xx driver)
 */
#define SSD1683_DISPLAY_INIT(n)                                                \
                                                                               \
  static struct ssd1683_display_data ssd1683_data_##n;                         \
                                                                               \
  static const struct ssd1683_display_config ssd1683_config_##n = {            \
      .epd_config =                                                            \
          {                                                                    \
              .bus = SPI_DT_SPEC_GET(n,                                        \
                                     SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB |   \
                                         SPI_WORD_SET(8) | SPI_MODE_CPOL |     \
                                         SPI_MODE_CPHA,                        \
                                     0),                                       \
              .dc = GPIO_DT_SPEC_GET(n, dc_gpios),                             \
              .rst = GPIO_DT_SPEC_GET(n, reset_gpios),                         \
              .busy = GPIO_DT_SPEC_GET(n, busy_gpios),                         \
              .width = DT_PROP(n, width),                                      \
              .height = DT_PROP(n, height),                                    \
          },                                                                   \
      .fast_mode = DT_PROP_OR(n, fast_mode, false),                            \
  };                                                                           \
                                                                               \
  DEVICE_DT_DEFINE(n, ssd1683_display_init, NULL, &ssd1683_data_##n,           \
                   &ssd1683_config_##n, POST_KERNEL,                           \
                   CONFIG_DISPLAY_INIT_PRIORITY, &ssd1683_display_api);

// Instantiate driver for all DT nodes with compatible = "solomon,ssd1683"
DT_FOREACH_STATUS_OKAY(solomon_ssd1683, SSD1683_DISPLAY_INIT)
