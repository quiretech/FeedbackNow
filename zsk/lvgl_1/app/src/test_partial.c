/*
 * Direct Partial Refresh Test
 * This file contains a test function to directly test
 * the SSD1683 partial refresh capability without LVGL
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_partial, LOG_LEVEL_INF);

/* Simple function to fill a buffer with a pattern */
static void fill_pattern(uint8_t *buf, int width_bytes, int height,
                         uint8_t pattern) {
  for (int i = 0; i < width_bytes * height; i++) {
    buf[i] = pattern;
  }
}

/**
 * Test partial refresh by directly calling display_write with a small region
 *
 * This bypasses LVGL and directly tests the driver's partial refresh capability
 */
void test_partial_refresh_direct(const struct device *display) {
  LOG_INF("=== DIRECT PARTIAL REFRESH TEST ===");

  // Test region: 100x50 pixels at position (150, 125)
  const int test_x = 150;
  const int test_y = 125;
  const int test_w = 100;
  const int test_h = 50;

  // Calculate buffer size (1 bit per pixel, 8 pixels per byte)
  const int width_bytes = (test_w + 7) / 8;
  const int buf_size = width_bytes * test_h;

  LOG_INF("Test region: x=%d, y=%d, w=%d, h=%d", test_x, test_y, test_w,
          test_h);
  LOG_INF("Buffer size: %d bytes (%d x %d)", buf_size, width_bytes, test_h);

  // Allocate buffer
  uint8_t *test_buf = k_malloc(buf_size);
  if (!test_buf) {
    LOG_ERR("Failed to allocate test buffer");
    return;
  }

  // Fill with black (0x00 = black)
  fill_pattern(test_buf, width_bytes, test_h, 0x00);

  // Setup display buffer descriptor
  struct display_buffer_descriptor desc = {
      .buf_size = buf_size,
      .width = test_w,
      .height = test_h,
      .pitch = width_bytes,
  };

  LOG_INF("Writing BLACK rectangle to display...");
  int ret = display_write(display, test_x, test_y, &desc, test_buf);
  if (ret < 0) {
    LOG_ERR("Display write failed: %d", ret);
  } else {
    LOG_INF("Partial write completed successfully!");
    LOG_INF("Check logs for 'Partial screen update' vs 'Full screen flush'");
  }

  // Wait 3 seconds
  k_sleep(K_SECONDS(3));

  // Now draw white rectangle (0xFF = white)
  fill_pattern(test_buf, width_bytes, test_h, 0xFF);
  LOG_INF("Writing WHITE rectangle to display...");
  ret = display_write(display, test_x, test_y, &desc, test_buf);
  if (ret < 0) {
    LOG_ERR("Display write failed: %d", ret);
  } else {
    LOG_INF("Partial write completed successfully!");
  }

  k_free(test_buf);
  LOG_INF("=== TEST COMPLETE ===");
}
