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

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ssd1683.h"

LOG_MODULE_REGISTER(ssd1683_display, CONFIG_DISPLAY_LOG_LEVEL);

#define DT_DRV_COMPAT solomon_ssd1683

// ============================================================================
// Display Driver Data Structures
// ============================================================================

/**
 * @brief SSD1683 display driver data structure
 */
struct ssd1683_display_data {
  bool is_initialized;
  bool is_blanked;
};

/**
 * @brief SSD1683 display driver configuration structure
 */
struct ssd1683_display_config {
  struct ssd1683_config epd_config;
  bool fast_mode;
};

// ============================================================================
// Display Driver API Implementation
// ============================================================================

/**
 * @brief Turn display blanking on (power off)
 */
static int ssd1683_display_blanking_on(const struct device *dev) {
  struct ssd1683_display_data *data = dev->data;
  int ret;

  LOG_DBG("Turning display blanking on");

  ret = ssd1683_power_off(dev);
  if (ret < 0) {
    LOG_ERR("Failed to power off display: %d", ret);
    return ret;
  }

  data->is_blanked = true;
  return 0;
}

/**
 * @brief Turn display blanking off (power on)
 */
static int ssd1683_display_blanking_off(const struct device *dev) {
  struct ssd1683_display_data *data = dev->data;
  int ret;

  LOG_DBG("Turning display blanking off");

  ret = ssd1683_power_on(dev);
  if (ret < 0) {
    LOG_ERR("Failed to power on display: %d", ret);
    return ret;
  }

  data->is_blanked = false;
  return 0;
}

/**
 * @brief Write image data to display
 */
static int ssd1683_display_write(const struct device *dev, const uint16_t x,
                                 const uint16_t y,
                                 const struct display_buffer_descriptor *desc,
                                 const void *buf) {
  struct ssd1683_display_data *data = dev->data;
  int ret;
  bool partial_update;

  LOG_DBG("Writing to display: x=%d, y=%d, w=%d, h=%d", x, y, desc->width,
          desc->height);

  // Validate parameters
  if (desc == NULL || buf == NULL) {
    LOG_ERR("Invalid parameters: desc=%p, buf=%p", desc, buf);
    return -EINVAL;
  }

  if (desc->width == 0 || desc->height == 0) {
    LOG_ERR("Invalid dimensions: w=%d, h=%d", desc->width, desc->height);
    return -EINVAL;
  }

  if (x + desc->width > SSD1683_WIDTH || y + desc->height > SSD1683_HEIGHT) {
    LOG_ERR("Write area exceeds display bounds: x=%d, y=%d, w=%d, h=%d", x, y,
            desc->width, desc->height);
    return -EINVAL;
  }

  // Ensure display is powered on
  if (data->is_blanked) {
    ret = ssd1683_display_blanking_off(dev);
    if (ret < 0) {
      return ret;
    }
  }

  // Determine if this is a partial or full update
  // Partial update if the write area is smaller than the full screen
  partial_update =
      (desc->width < SSD1683_WIDTH) || (desc->height < SSD1683_HEIGHT);

  LOG_DBG("Update type: %s", partial_update ? "partial" : "full");

  // STEP 1: Write image data to CURRENT buffer (0x24)
  ret = ssd1683_write_image(dev, (const uint8_t *)buf, x, y, desc->width,
                            desc->height, false, false);
  if (ret < 0) {
    LOG_ERR("Failed to write image data: %d", ret);
    return ret;
  }

  // STEP 2: Sync PREVIOUS buffer (0x26) with CURRENT buffer
  // CRITICAL for partial refresh - syncs both buffers to prevent ghosting
  ret = ssd1683_write_image_again(dev, (const uint8_t *)buf, x, y, desc->width,
                                  desc->height, false, false);
  if (ret < 0) {
    LOG_ERR("Failed to sync buffers: %d", ret);
    // Continue anyway - current buffer already written
  }

  // STEP 3: Refresh the display (partial or full)
  ret = ssd1683_refresh(dev, false);
  if (ret < 0) {
    LOG_ERR("Failed to refresh display: %d", ret);
    return ret;
  }

  LOG_DBG("Display write completed successfully");
  return 0;
}

/**
 * @brief Get display capabilities
 */
static void
ssd1683_display_get_capabilities(const struct device *dev,
                                 struct display_capabilities *capabilities) {
  if (capabilities == NULL) {
    return;
  }

  LOG_DBG("Getting display capabilities");

  capabilities->x_resolution = SSD1683_WIDTH;
  capabilities->y_resolution = SSD1683_HEIGHT;
  capabilities->supported_pixel_formats = PIXEL_FORMAT_MONO01;
  capabilities->current_pixel_format = PIXEL_FORMAT_MONO01;
  capabilities->screen_info = SCREEN_INFO_MONO_MSB_FIRST | SCREEN_INFO_EPD;
}

/**
 * @brief Set pixel format (not supported for e-paper displays)
 */
static int
ssd1683_display_set_pixel_format(const struct device *dev,
                                 const enum display_pixel_format pixel_format) {
  LOG_DBG("Set pixel format requested: %d", pixel_format);

  // E-paper displays only support MONO01 format
  if (pixel_format != PIXEL_FORMAT_MONO01) {
    LOG_WRN("Pixel format %d not supported, only MONO01 is supported",
            pixel_format);
    return -ENOTSUP;
  }

  return 0;
}

/**
 * @brief Set display orientation (not supported for e-paper displays)
 */
static int
ssd1683_display_set_orientation(const struct device *dev,
                                const enum display_orientation orientation) {
  LOG_DBG("Set orientation requested: %d", orientation);

  // E-paper displays have fixed orientation
  LOG_WRN("Orientation change not supported for e-paper displays");
  return -ENOTSUP;
}

// ============================================================================
// Zephyr Display Driver API Structure
// ============================================================================

static const struct display_driver_api ssd1683_display_api = {
    .blanking_on = ssd1683_display_blanking_on,
    .blanking_off = ssd1683_display_blanking_off,
    .write = ssd1683_display_write,
    .get_capabilities = ssd1683_display_get_capabilities,
    .set_pixel_format = ssd1683_display_set_pixel_format,
    .set_orientation = ssd1683_display_set_orientation,
    // .clear = ssd1683_display_clear,
    // Not implemented (return -ENOTSUP by default):
    // .read = NULL,
    // .get_framebuffer = NULL,
    // .set_brightness = NULL,
    // .set_contrast = NULL,
};

// ============================================================================
// Display Driver Initialization
// ============================================================================

/**
 * @brief Initialize the SSD1683 display driver
 */
static int ssd1683_display_init(const struct device *dev) {
  const struct ssd1683_display_config *config = dev->config;
  struct ssd1683_display_data *data = dev->data;
  int ret;

  LOG_INF("Initializing SSD1683 display driver");

  // Initialize the low-level SSD1683 driver
  ret = ssd1683_init(dev, &config->epd_config);
  if (ret < 0) {
    LOG_ERR("Failed to initialize SSD1683 driver: %d", ret);
    return ret;
  }

  // Set initial state
  data->is_initialized = true;
  data->is_blanked = false;

  LOG_INF("SSD1683 display driver initialized successfully");
  return 0;
}

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