/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 *
 * SSD1683 E-Paper Display Example
 *
 * This example demonstrates how to use the SSD1683 device driver API.
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ssd1683.h"

LOG_MODULE_REGISTER(ssd1683_example, LOG_LEVEL_INF);

// Example image data (400x300 pixels = 15000 bytes)
static const uint8_t example_image[] = {
    // This would contain actual image data
    // For demo purposes, we'll use a simple pattern
};

void main(void) {
  const struct device *display = DEVICE_DT_GET_ONE(solomon_ssd1683);
  struct ssd1683_capabilities caps;
  int ret;

  if (!device_is_ready(display)) {
    LOG_ERR("SSD1683 display not ready");
    return;
  }

  LOG_INF("SSD1683 E-Paper Display Example");

  // Get display capabilities
  ret = ssd1683_get_capabilities(display, &caps);
  if (ret < 0) {
    LOG_ERR("Failed to get capabilities: %d", ret);
    return;
  }

  LOG_INF("Display: %dx%d, fast=%d, partial=%d, 4g=%d, rot=%d", caps.width,
          caps.height, caps.fast_mode_supported, caps.partial_refresh_supported,
          caps.four_gray_supported, caps.rotation_supported);

  // Initialize display
  ret = ssd1683_init(display);
  if (ret < 0) {
    LOG_ERR("Failed to initialize display: %d", ret);
    return;
  }

  // Set fast mode for quicker updates
  ret = ssd1683_set_mode(display, SSD1683_MODE_FAST);
  if (ret < 0) {
    LOG_ERR("Failed to set fast mode: %d", ret);
    return;
  }

  // Set refresh type to fast
  ret = ssd1683_set_refresh_type(display, SSD1683_REFRESH_FAST);
  if (ret < 0) {
    LOG_ERR("Failed to set refresh type: %d", ret);
    return;
  }

  // Clear display
  ret = ssd1683_clear(display);
  if (ret < 0) {
    LOG_ERR("Failed to clear display: %d", ret);
    return;
  }

  LOG_INF("Display cleared successfully");

  // Display full screen image
  ret = ssd1683_display_image(display, example_image);
  if (ret < 0) {
    LOG_ERR("Failed to display image: %d", ret);
    return;
  }

  LOG_INF("Image displayed successfully");

  // Example of partial update
  uint8_t partial_data[100];                        // Small partial image
  memset(partial_data, 0xAA, sizeof(partial_data)); // Pattern

  ret = ssd1683_display_partial(display, 50, 50, 100, 50, partial_data);
  if (ret < 0) {
    LOG_ERR("Failed to display partial image: %d", ret);
    return;
  }

  LOG_INF("Partial image displayed successfully");

  // Put display to sleep to save power
  ret = ssd1683_sleep(display);
  if (ret < 0) {
    LOG_ERR("Failed to put display to sleep: %d", ret);
    return;
  }

  LOG_INF("Display put to sleep");

  // Wait a bit
  k_msleep(5000);

  // Wake display
  ret = ssd1683_wake(display);
  if (ret < 0) {
    LOG_ERR("Failed to wake display: %d", ret);
    return;
  }

  LOG_INF("Display woken up");

  // Clear display again
  ssd1683_clear(display);
  LOG_INF("Display cleared again");

  LOG_INF("SSD1683 example completed successfully");
}
