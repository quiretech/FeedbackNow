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
// Driver Data Structures
// ============================================================================

/**
 * Per-instance runtime data (RAM)
 * Currently minimal, can be extended for framebuffer caching if needed
 */
struct ssd1683_display_data {
  bool blanking_on; // Track blanking state
};

/**
 * Per-instance configuration (ROM) - populated from device tree
 * This wraps our existing ssd1683_config structure
 */
struct ssd1683_display_config {
  struct ssd1683_config epd_config; // Embed low-level config
  bool fast_mode;                   // Use fast init/refresh
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Get the low-level EPD config from device
 */
static inline const struct ssd1683_config *
get_epd_config(const struct device *dev) {
  const struct ssd1683_display_config *cfg = dev->config;
  return &cfg->epd_config;
}

// ============================================================================
// Zephyr Display API Implementation
// ============================================================================

/**
 * Turn display blanking on (enter deep sleep mode)
 */
static int ssd1683_display_blanking_on(const struct device *dev) {
  struct ssd1683_display_data *data = dev->data;
  const struct ssd1683_config *epd_cfg = get_epd_config(dev);

  LOG_INF("Blanking ON (deep sleep)");
  ssd1683_deep_sleep(epd_cfg);
  data->blanking_on = true;

  return 0;
}

/**
 * Turn display blanking off (wake from deep sleep and re-initialize)
 */
static int ssd1683_display_blanking_off(const struct device *dev) {
  struct ssd1683_display_data *data = dev->data;
  const struct ssd1683_display_config *cfg = dev->config;
  int ret;

  LOG_INF("Blanking OFF (wake up)");

  // Re-initialize display after waking from deep sleep
  if (cfg->fast_mode) {
    ret = ssd1683_init_fast(&cfg->epd_config);
  } else {
    ret = ssd1683_init(&cfg->epd_config);
  }

  if (ret == 0) {
    data->blanking_on = false;
  }

  return ret;
}

/**
 * Write framebuffer data to display
 *
 * This is the core function that LVGL and other graphics libraries will call.
 */
static int ssd1683_display_write(const struct device *dev, const uint16_t x,
                                 const uint16_t y,
                                 const struct display_buffer_descriptor *desc,
                                 const void *buf) {
  const struct ssd1683_config *epd_cfg = get_epd_config(dev);

  LOG_DBG("Write: x=%d, y=%d, w=%d, h=%d, pitch=%d, buf_size=%d", x, y,
          desc->width, desc->height, desc->pitch, desc->buf_size);

  // Full screen update (most common case)
  if (x == 0 && y == 0 && desc->width == epd_cfg->width &&
      desc->height == epd_cfg->height) {

    LOG_DBG("Full screen flush");
    ssd1683_flush(epd_cfg, (uint8_t *)buf);
    return 0;
  }

  // Partial screen update
  LOG_DBG("Partial screen update");
  ssd1683_write_display(epd_cfg, x, y, desc->width, desc->height,
                        (uint8_t *)buf);
  ssd1683_refresh_partial(epd_cfg);

  return 0;
}

/**
 * Clear the display (set all pixels to white)
 *
 * Uses the low-level clear function from the driver
 */
static int ssd1683_display_clear(const struct device *dev) {
  const struct ssd1683_config *epd_cfg = get_epd_config(dev);

  LOG_INF("Clearing display");

  // Use low-level clear function
  ssd1683_clear(epd_cfg);
  ssd1683_refresh(epd_cfg);

  return 0;
}

/**
 * Get display capabilities
 *
 * Reports display specifications to the graphics framework
 */
static void
ssd1683_display_get_capabilities(const struct device *dev,
                                 struct display_capabilities *caps) {
  const struct ssd1683_config *epd_cfg = get_epd_config(dev);

  memset(caps, 0, sizeof(struct display_capabilities));

  caps->x_resolution = epd_cfg->width;
  caps->y_resolution = epd_cfg->height;

  // Pixel format: MONO10 = 1 bit/pixel, 1=white, 0=black
  caps->supported_pixel_formats = PIXEL_FORMAT_MONO10;
  caps->current_pixel_format = PIXEL_FORMAT_MONO10;

  // Screen info:
  // - MONO_MSB_FIRST: MSB is leftmost pixel
  // - EPD: Electrophoretic Display
  caps->screen_info = SCREEN_INFO_MONO_MSB_FIRST | SCREEN_INFO_EPD;

  caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;

  LOG_DBG("Capabilities: %dx%d, MONO10, MSB_FIRST, EPD", caps->x_resolution,
          caps->y_resolution);
}

/**
 * Set pixel format (not supported - format is fixed)
 */
static int
ssd1683_display_set_pixel_format(const struct device *dev,
                                 const enum display_pixel_format pf) {
  if (pf == PIXEL_FORMAT_MONO10) {
    return 0; // Already in correct format
  }

  LOG_ERR("Unsupported pixel format: %d", pf);
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
    // Not implemented (return -ENOTSUP by default):
    // .read = NULL,
    // .get_framebuffer = NULL,
    // .set_brightness = NULL,
    // .set_contrast = NULL,
    // .set_orientation = NULL,
};

// ============================================================================
// Device Initialization
// ============================================================================

/**
 * Initialize the display driver
 *
 * Called by Zephyr during boot (POST_KERNEL phase)
 */
static int ssd1683_display_init(const struct device *dev) {
  const struct ssd1683_display_config *cfg = dev->config;
  struct ssd1683_display_data *data = dev->data;
  int ret;

  LOG_INF("Initializing SSD1683 display driver");

  // Check if SPI bus is ready
  if (!spi_is_ready_dt(&cfg->epd_config.bus)) {
    LOG_ERR("SPI bus not ready");
    return -ENODEV;
  }

  // Check if GPIO devices are ready
  if (!gpio_is_ready_dt(&cfg->epd_config.dc)) {
    LOG_ERR("DC GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&cfg->epd_config.rst)) {
    LOG_ERR("RST GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&cfg->epd_config.busy)) {
    LOG_ERR("BUSY GPIO not ready");
    return -ENODEV;
  }

  // Initialize the low-level EPD driver
  if (cfg->fast_mode) {
    LOG_INF("Using fast mode initialization");
    ret = ssd1683_init_fast(&cfg->epd_config);
  } else {
    LOG_INF("Using standard mode initialization");
    ret = ssd1683_init(&cfg->epd_config);
  }

  if (ret < 0) {
    LOG_ERR("EPD initialization failed: %d", ret);
    return ret;
  }

  // Clear display on startup
  ssd1683_clear(&cfg->epd_config);
  ssd1683_refresh(&cfg->epd_config);

  data->blanking_on = false;

  LOG_INF("SSD1683 display driver initialized successfully");
  return 0;
}

// ============================================================================
// Device Instantiation Macro
// ============================================================================

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
