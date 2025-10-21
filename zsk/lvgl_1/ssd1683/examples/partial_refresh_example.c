/*
 * Example: SSD1683 Partial Refresh Usage
 *
 * This example demonstrates how to use the partial refresh functionality
 * for efficient e-paper display updates without flickering.
 */

#include "ssd1683.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(partial_refresh_example, LOG_LEVEL_INF);

// Example usage of partial refresh functions
void partial_refresh_demo(const struct ssd1683_config *cfg) {
  // Example image data (400x300 pixels = 15000 bytes)
  static uint8_t background_data[15000];
  static uint8_t partial_data[1000]; // 100x80 pixel region

  LOG_INF("Starting partial refresh demo");

  // Step 1: Initialize for partial refresh
  ssd1683_hw_init_partial(cfg);

  // Step 2: Set background image (CRITICAL for stable partial refresh)
  // Fill background with white (0xFF = white, 0x00 = black)
  for (int i = 0; i < 15000; i++) {
    background_data[i] = 0xFF; // White background
  }
  ssd1683_set_base_map(cfg, background_data, 15000);

  // Step 3: Perform partial updates
  // Update a small region at position (100, 50) with size 100x80 pixels
  for (int i = 0; i < 1000; i++) {
    partial_data[i] = 0x00; // Black pattern
  }
  ssd1683_partial_refresh(cfg, 100, 50, partial_data, 100, 80);

  k_msleep(2000); // Wait 2 seconds

  // Step 4: Another partial update
  // Update a different region at position (200, 100) with size 80x60 pixels
  uint8_t another_partial[600]; // 80x60 pixels
  for (int i = 0; i < 600; i++) {
    another_partial[i] = 0xAA; // Gray pattern
  }
  ssd1683_partial_refresh(cfg, 200, 100, another_partial, 80, 60);

  k_msleep(2000);

  // Step 5: Full screen partial refresh (faster than full refresh)
  uint8_t full_screen_data[15000];
  for (int i = 0; i < 15000; i++) {
    full_screen_data[i] = 0x55; // Gray pattern
  }
  ssd1683_partial_refresh_full(cfg, full_screen_data, 15000);

  k_msleep(2000);

  // Step 6: After multiple partial refreshes, do a full refresh to clear
  // ghosting
  ssd1683_hw_init(cfg);   // Full refresh initialization
  ssd1683_fillwhite(cfg); // Clear screen

  // Step 7: Enter sleep mode
  ssd1683_deep_sleep(cfg);

  LOG_INF("Partial refresh demo completed");
}

/*
 * Usage Notes:
 *
 * 1. ALWAYS call ssd1683_set_base_map() before any partial refresh
 * 2. After 5 partial refreshes, do a full refresh to clear ghosting
 * 3. X coordinates are automatically byte-aligned (divided by 8)
 * 4. Y coordinates are used as-is
 * 5. Always call ssd1683_deep_sleep() when done
 *
 * Partial refresh is ideal for:
 * - Clock displays
 * - Real-time data updates
 * - Battery-powered applications
 * - Any application requiring fast, flicker-free updates
 */
