/*
 * SSD1683 E-Paper Display Demo
 *
 * This application demonstrates the use of the SSD1683 raw driver
 * with device tree configuration for partial refresh functionality.
 */

// #include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "ssd1683.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// Device tree node references
#define SPI_NODE DT_NODELABEL(my_spi_master)
#define EPD_BUSY_NODE DT_PATH(zephyr_user, epd_busy_gpios)
#define EPD_DC_NODE DT_PATH(zephyr_user, epd_dc_gpios)
#define EPD_RST_NODE DT_PATH(zephyr_user, epd_rst_gpios)

// Display configuration
#define DISPLAY_WIDTH 400
#define DISPLAY_HEIGHT 300
#define DISPLAY_ARRAY (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

// SPI configuration
static const struct spi_dt_spec spi_bus =
    SPI_DT_SPEC_INST_GET(0, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);

// GPIO configurations
static const struct gpio_dt_spec epd_busy =
    GPIO_DT_SPEC_GET(EPD_BUSY_NODE, gpios);
static const struct gpio_dt_spec epd_dc = GPIO_DT_SPEC_GET(EPD_DC_NODE, gpios);
static const struct gpio_dt_spec epd_rst =
    GPIO_DT_SPEC_GET(EPD_RST_NODE, gpios);

// SSD1683 configuration structure
static struct ssd1683_config ssd1683_cfg = {.bus = spi_bus,
                                            .dc = epd_dc,
                                            .rst = epd_rst,
                                            .busy = epd_busy,
                                            .width = DISPLAY_WIDTH,
                                            .height = DISPLAY_HEIGHT};

// Test image data
static uint8_t test_image[DISPLAY_ARRAY];
static uint8_t background_image[DISPLAY_ARRAY];

// Function to create a simple test pattern
static void create_test_pattern(uint8_t *image, uint16_t width,
                                uint16_t height) {
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width / 8; x++) {
      int index = y * (width / 8) + x;
      // Create a checkerboard pattern
      if ((x + y) % 2 == 0) {
        image[index] = 0xAA; // Gray pattern
      } else {
        image[index] = 0x55; // Different gray pattern
      }
    }
  }
}

// Function to create a simple geometric pattern
static void create_geometric_pattern(uint8_t *image, uint16_t width,
                                     uint16_t height) {
  // Clear image first
  for (int i = 0; i < DISPLAY_ARRAY; i++) {
    image[i] = 0xFF; // White background
  }

  // Draw some simple shapes
  for (int y = 50; y < 150; y++) {
    for (int x = 50; x < 150; x++) {
      int byte_index = y * (width / 8) + (x / 8);
      int bit_index = 7 - (x % 8);
      image[byte_index] &= ~(1 << bit_index); // Set pixel to black
    }
  }
}

// Partial refresh demo function
static void partial_refresh_demo(void) {
  LOG_INF("Starting partial refresh demo");

  // Step 1: Initialize for partial refresh
  ssd1683_hw_init_partial(&ssd1683_cfg);

  // Step 2: Create and set background image
  create_geometric_pattern(background_image, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  ssd1683_set_base_map(&ssd1683_cfg, background_image, DISPLAY_ARRAY);

  k_msleep(2000);

  // Step 3: Partial refresh - update a small region
  uint8_t partial_data[1000]; // 100x80 pixel region
  for (int i = 0; i < 1000; i++) {
    partial_data[i] = 0x00; // Black pattern
  }
  ssd1683_partial_refresh(&ssd1683_cfg, 100, 50, partial_data, 100, 80);

  k_msleep(2000);

  // Step 4: Another partial update
  uint8_t another_partial[600]; // 80x60 pixels
  for (int i = 0; i < 600; i++) {
    another_partial[i] = 0xAA; // Gray pattern
  }
  ssd1683_partial_refresh(&ssd1683_cfg, 200, 100, another_partial, 80, 60);

  k_msleep(2000);

  // Step 5: Full screen partial refresh
  create_test_pattern(test_image, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  ssd1683_partial_refresh_full(&ssd1683_cfg, test_image, DISPLAY_ARRAY);

  k_msleep(2000);

  LOG_INF("Partial refresh demo completed");
}

// Full refresh demo function
static void full_refresh_demo(void) {
  LOG_INF("Starting full refresh demo");

  // Step 1: Initialize for full refresh
  ssd1683_hw_init(&ssd1683_cfg);

  // Step 2: Clear screen
  ssd1683_fillwhite(&ssd1683_cfg);
  k_msleep(1000);

  // Step 3: Fill with black
  ssd1683_fillblack(&ssd1683_cfg);
  k_msleep(1000);

  // Step 4: Display test pattern
  create_test_pattern(test_image, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  ssd1683_write_ram_bw(&ssd1683_cfg, test_image, DISPLAY_ARRAY);
  ssd1683_update(&ssd1683_cfg);

  k_msleep(2000);

  LOG_INF("Full refresh demo completed");
}

// Fast refresh demo function
static void fast_refresh_demo(void) {
  LOG_INF("Starting fast refresh demo");

  // Step 1: Initialize for fast refresh
  ssd1683_hw_init_fast(&ssd1683_cfg);

  // Step 2: Display test pattern with fast refresh
  create_geometric_pattern(test_image, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  ssd1683_write_ram_bw(&ssd1683_cfg, test_image, DISPLAY_ARRAY);
  ssd1683_update_fast(&ssd1683_cfg);

  k_msleep(2000);

  LOG_INF("Fast refresh demo completed");
}

// Main application function
int main(void) {
  LOG_INF("SSD1683 E-Paper Display Demo Starting");

  // Check if all devices are ready
  if (!spi_is_ready_dt(&spi_bus)) {
    LOG_ERR("SPI bus not ready");
    return -ENODEV;
  }

  if (!gpio_is_ready_dt(&epd_busy)) {
    LOG_ERR("BUSY GPIO not ready");
    return -ENODEV;
  }

  if (!gpio_is_ready_dt(&epd_dc)) {
    LOG_ERR("DC GPIO not ready");
    return -ENODEV;
  }

  if (!gpio_is_ready_dt(&epd_rst)) {
    LOG_ERR("RST GPIO not ready");
    return -ENODEV;
  }

  LOG_INF("All devices ready, starting demo sequence");

  // Demo sequence
  while (1) {
    LOG_INF("=== Full Refresh Demo ===");
    full_refresh_demo();
    k_msleep(3000);

    LOG_INF("=== Fast Refresh Demo ===");
    fast_refresh_demo();
    k_msleep(3000);

    LOG_INF("=== Partial Refresh Demo ===");
    partial_refresh_demo();
    k_msleep(3000);

    // Clear screen and sleep
    ssd1683_hw_init(&ssd1683_cfg);
    ssd1683_fillwhite(&ssd1683_cfg);
    ssd1683_deep_sleep(&ssd1683_cfg);

    LOG_INF("Demo cycle completed, sleeping for 10 seconds");
    k_msleep(10000);
  }

  return 0;
}
