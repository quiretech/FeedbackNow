/**
 * @file example_usage.c
 * @brief Example usage of the refactored SSD1683 driver
 *
 * This demonstrates how to use the clean Zephyr-style SSD1683 driver
 * with proper state management and error handling.
 */

#include "ssd1683.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ssd1683_example, LOG_LEVEL_INF);

// Example configuration structure
static const struct ssd1683_config ssd1683_cfg = {
    .bus = SPI_DT_SPEC_INST_GET(0),
    .dc = GPIO_DT_SPEC_INST_GET(0, dc_gpios),
    .rst = GPIO_DT_SPEC_INST_GET(0, rst_gpios),
    .busy = GPIO_DT_SPEC_INST_GET(0, busy_gpios),
    .width = SSD1683_WIDTH,
    .height = SSD1683_HEIGHT,
};

// Example device data structure
static struct ssd1683_data ssd1683_data;

// Example device structure (for future Zephyr driver API compatibility)
static const struct device ssd1683_dev = {
    .config = &ssd1683_cfg,
    .data = &ssd1683_data,
};

/**
 * @brief Basic usage example with proper error handling
 */
void ssd1683_example_basic_usage(void) {
  int ret;

  LOG_INF("SSD1683 Basic Usage Example");

  // 1. Initialize the driver
  ret = ssd1683_init(&ssd1683_dev, &ssd1683_cfg);
  if (ret < 0) {
    LOG_ERR("Failed to initialize SSD1683: %d", ret);
    return;
  }

  // 2. Power on the display
  ret = ssd1683_power_on(&ssd1683_dev);
  if (ret < 0) {
    LOG_ERR("Failed to power on SSD1683: %d", ret);
    return;
  }

  // 3. Clear screen to white (like reference clearScreen)
  ret = ssd1683_clear_screen(&ssd1683_dev, 0xFF);
  if (ret < 0) {
    LOG_ERR("Failed to clear screen: %d", ret);
    return;
  }

  // 4. Set fast full update mode
  ret = ssd1683_set_fast_update(&ssd1683_dev, true);
  if (ret < 0) {
    LOG_ERR("Failed to set fast update: %d", ret);
    return;
  }

  LOG_INF("Basic initialization completed");
}

/**
 * @brief Image display example with error handling
 */
void ssd1683_example_image_display(void) {
  int ret;

  LOG_INF("SSD1683 Image Display Example");

  // Example bitmap data (8x8 pixels = 8 bytes)
  static const uint8_t test_bitmap[] = {
      0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF // Simple border pattern
  };

  // Display the bitmap at position (10, 10) with size 8x8
  ret = ssd1683_write_image(&ssd1683_dev, test_bitmap, 10, 10, 8, 8, false,
                            false);
  if (ret < 0) {
    LOG_ERR("Failed to write image: %d", ret);
    return;
  }

  // Refresh the display (full update)
  ret = ssd1683_refresh(&ssd1683_dev, false);
  if (ret < 0) {
    LOG_ERR("Failed to refresh display: %d", ret);
    return;
  }

  LOG_INF("Image display completed");
}

/**
 * @brief Partial update example
 */
void ssd1683_example_partial_update(void) {
  int ret;

  LOG_INF("SSD1683 Partial Update Example");

  // Example bitmap for partial update
  static const uint8_t partial_bitmap[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Black pattern
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  // Write image to partial area
  ret = ssd1683_write_image(&ssd1683_dev, partial_bitmap, 50, 50, 64, 64, false,
                            false);
  if (ret < 0) {
    LOG_ERR("Failed to write partial image: %d", ret);
    return;
  }

  // Refresh with partial update
  ret = ssd1683_refresh(&ssd1683_dev, true);
  if (ret < 0) {
    LOG_ERR("Failed to perform partial refresh: %d", ret);
    return;
  }

  LOG_INF("Partial update completed");
}

/**
 * @brief Power management example
 */
void ssd1683_example_power_management(void) {
  int ret;

  LOG_INF("SSD1683 Power Management Example");

  // Check current power state
  bool is_powered = ssd1683_is_powered_on(&ssd1683_dev);
  LOG_INF("Current power state: %s", is_powered ? "ON" : "OFF");

  // Power off the display
  ret = ssd1683_power_off(&ssd1683_dev);
  if (ret < 0) {
    LOG_ERR("Failed to power off SSD1683: %d", ret);
    return;
  }

  // Wait a bit
  k_msleep(1000);

  // Power back on
  ret = ssd1683_power_on(&ssd1683_dev);
  if (ret < 0) {
    LOG_ERR("Failed to power on SSD1683: %d", ret);
    return;
  }

  // Or put into hibernate mode for lowest power
  ret = ssd1683_hibernate(&ssd1683_dev);
  if (ret < 0) {
    LOG_ERR("Failed to hibernate SSD1683: %d", ret);
    return;
  }

  LOG_INF("Power management example completed");
}

/**
 * @brief Complete workflow example
 */
void ssd1683_example_complete_workflow(void) {
  int ret;

  LOG_INF("SSD1683 Complete Workflow Example");

  // Complete workflow: init -> clear -> draw -> refresh -> power off
  ret = ssd1683_init(&ssd1683_dev, &ssd1683_cfg);
  if (ret < 0) {
    LOG_ERR("Failed to initialize: %d", ret);
    return;
  }

  ret = ssd1683_power_on(&ssd1683_dev);
  if (ret < 0) {
    LOG_ERR("Failed to power on: %d", ret);
    return;
  }

  ret = ssd1683_clear_screen(&ssd1683_dev, 0xFF);
  if (ret < 0) {
    LOG_ERR("Failed to clear screen: %d", ret);
    return;
  }

  // Draw some content
  static const uint8_t content[] = {
      0x3C, 0x42, 0x81, 0x81, 0x81, 0x42, 0x3C, 0x00 // Simple circle pattern
  };

  ret = ssd1683_write_image(&ssd1683_dev, content, 20, 20, 8, 8, false, false);
  if (ret < 0) {
    LOG_ERR("Failed to write content: %d", ret);
    return;
  }

  // Refresh display
  ret = ssd1683_refresh(&ssd1683_dev, false);
  if (ret < 0) {
    LOG_ERR("Failed to refresh: %d", ret);
    return;
  }

  // Wait for display to settle
  k_msleep(2000);

  // Power off when done
  ret = ssd1683_power_off(&ssd1683_dev);
  if (ret < 0) {
    LOG_ERR("Failed to power off: %d", ret);
    return;
  }

  LOG_INF("Complete workflow example finished");
}

/**
 * @brief State monitoring example
 */
void ssd1683_example_state_monitoring(void) {
  LOG_INF("SSD1683 State Monitoring Example");

  // Check initialization state
  bool is_init = ssd1683_is_initialized(&ssd1683_dev);
  LOG_INF("Initialization state: %s",
          is_init ? "INITIALIZED" : "NOT INITIALIZED");

  // Check power state
  bool is_powered = ssd1683_is_powered_on(&ssd1683_dev);
  LOG_INF("Power state: %s", is_powered ? "ON" : "OFF");

  // Initialize if needed
  if (!is_init) {
    int ret = ssd1683_init(&ssd1683_dev, &ssd1683_cfg);
    if (ret < 0) {
      LOG_ERR("Failed to initialize: %d", ret);
      return;
    }
    LOG_INF("Driver initialized");
  }

  LOG_INF("State monitoring completed");
}

/**
 * @brief Main application entry point
 * This would typically be called from your main application
 */
void ssd1683_example_main(void) {
  LOG_INF("Starting SSD1683 Driver Examples");

  // Run all examples with proper error handling
  ssd1683_example_basic_usage();
  k_msleep(1000);

  ssd1683_example_image_display();
  k_msleep(2000);

  ssd1683_example_partial_update();
  k_msleep(2000);

  ssd1683_example_power_management();
  k_msleep(1000);

  ssd1683_example_state_monitoring();
  k_msleep(1000);

  ssd1683_example_complete_workflow();

  LOG_INF("All SSD1683 examples completed");
}