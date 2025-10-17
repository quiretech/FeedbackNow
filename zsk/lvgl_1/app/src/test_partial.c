/*
 * Direct Partial Refresh Test
 * This file contains a test function to directly test
 * the SSD1683 partial refresh capability.
 */

#include "cfb_mono_FreeSans.h"
#include "cfb_mono_FreeSansBold.h"
#include "fonts/cfbv_1016.h"
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
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
 * This function tests the driver's partial refresh capability without using
 * LVGL.
 */
void test_partial_refresh_direct(const struct device *display) {
  LOG_INF("=== DIRECT PARTIAL REFRESH TEST ===");

  // First, clear the entire display to white background

  // Wait a moment for the clear to complete
  k_sleep(K_SECONDS(2));

  // Center the box if (0,0) is bottom left
  const int screen_w = 400; // Set to your display's width in pixels
  const int screen_h = 300; // Set to your display's height in pixels
  const int test_w = 30;
  const int test_h = 30;
  const int test_x = (screen_w - test_w) / 2;
  const int test_y = (screen_h - test_h) / 2;

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

/**
 * Test CFB (Character Framebuffer) text rendering
 *
 * This demonstrates how to use Zephyr's CFB to draw text on the display.
 */
void test_cfb_text(const struct device *display) {
  LOG_INF("=== CFB TEXT RENDERING TEST ===");

  // Initialize CFB
  int ret = cfb_framebuffer_init(display);
  if (ret != 0) {
    LOG_ERR("CFB init failed: %d", ret);
    return;
  }
  LOG_INF("CFB initialized successfully");

  // Set custom font (index 0 for our custom font)
  ret = cfb_framebuffer_set_font(display, 0);
  if (ret != 0) {
    LOG_ERR("CFB font set failed: %d", ret);
    return;
  }
  LOG_INF("Custom font (10x16) set successfully");

  // Clear display first
  cfb_framebuffer_clear(display, true);
  LOG_INF("Display cleared");

  // Draw some text at different positions
  cfb_print(display, "Hello World!", 10, 10);
  cfb_print(display, "CFB Test!", 10, 30);
  cfb_print(display, "E-Paper Display", 10, 50);

  // Draw a counter
  static int counter = 0;
  char counter_str[32];
  snprintf(counter_str, sizeof(counter_str), "Counter: %d", counter++);
  cfb_print(display, counter_str, 10, 70);

  // Draw some more text to fill the screen
  cfb_print(display, "Zephyr RTOS", 10, 90);
  cfb_print(display, "SSD1683 Driver", 10, 110);
  cfb_print(display, "400x300 Display", 10, 130);
  cfb_print(display, "Custom Font 10x16", 10, 150);

  // Draw text at different positions
  cfb_print(display, "Top Right", 300, 10);
  cfb_print(display, "Bottom Left", 10, 250);
  cfb_print(display, "Center", 180, 150);

  // Show font info
  cfb_print(display, "Font: 10x16 pixels", 10, 170);

  // Finalize and refresh
  cfb_framebuffer_finalize(display);
  LOG_INF("CFB text rendered successfully");
}

/**
 * Test CFB with different fonts
 *
 * This demonstrates switching between different CFB fonts
 */
void test_cfb_multiple_fonts(const struct device *display) {
  LOG_INF("=== CFB MULTIPLE FONTS TEST ===");

  // Initialize CFB
  int ret = cfb_framebuffer_init(display);
  if (ret != 0) {
    LOG_ERR("CFB init failed: %d", ret);
    return;
  }
  LOG_INF("CFB initialized successfully");

  // Test with original custom font (10x16) - index 0
  ret = cfb_framebuffer_set_font(display, 0);
  if (ret != 0) {
    LOG_ERR("CFB font set failed: %d", ret);
    return;
  }
  LOG_INF("Using custom font (10x16)");

  // Clear and draw with first font
  cfb_framebuffer_clear(display, true);
  cfb_print(display, "Font 1: Custom 10x16", 10, 10);
  cfb_print(display, "Hello World!", 10, 30);
  cfb_framebuffer_finalize(display);

  k_sleep(K_SECONDS(2));

  // Test with FreeSans font (12x16) - index 1
  ret = cfb_framebuffer_set_font(display, 1);
  if (ret == 0) {
    LOG_INF("Using FreeSans font (12x16)");
    cfb_framebuffer_clear(display, true);
    cfb_print(display, "Font 2: FreeSans 12x16", 10, 10);
    cfb_print(display, "Hello World!", 10, 30);
    cfb_framebuffer_finalize(display);
    k_sleep(K_SECONDS(2));
  }

  // Test with FreeSansBold font (12x16) - index 2
  ret = cfb_framebuffer_set_font(display, 2);
  if (ret == 0) {
    LOG_INF("Using FreeSansBold font (12x16)");
    cfb_framebuffer_clear(display, true);
    cfb_print(display, "Font 3: FreeSansBold 12x16", 10, 10);
    cfb_print(display, "Hello World!", 10, 30);
    cfb_framebuffer_finalize(display);
    k_sleep(K_SECONDS(2));
  }

  LOG_INF("CFB multiple fonts test completed");
}
