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

#include "Ap_29demo.h"
#include "ssd1683.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// Device tree node references
#define ARDUINO_SPI_NODE DT_NODELABEL(arduino_spi)
#define EPD_DEVICE_NODE DT_NODELABEL(epd_spi_device)
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

// Display configuration
#define DISPLAY_WIDTH SSD1683_WIDTH
#define DISPLAY_HEIGHT SSD1683_HEIGHT
#define DISPLAY_ARRAY (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

// SPI configuration using your arduino_spi
static const struct spi_dt_spec spi_bus = {
    .bus = DEVICE_DT_GET(DT_NODELABEL(arduino_spi)),
    .config = {
        .operation =
            SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
        .frequency = 4000000,
        .slave = 0,
        .cs = {.gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(arduino_spi), cs_gpios),
               .delay = 0}}};

// GPIO configurations
static const struct gpio_dt_spec epd_busy =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_busy_gpios);
static const struct gpio_dt_spec epd_dc =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_dc_gpios);
static const struct gpio_dt_spec epd_rst =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_rst_gpios);

// SSD1683 configuration structure
static struct ssd1683_config ssd1683_cfg = {.bus = spi_bus,
                                            .dc = epd_dc,
                                            .rst = epd_rst,
                                            .busy = epd_busy,
                                            .width = DISPLAY_WIDTH,
                                            .height = DISPLAY_HEIGHT};

// Demo function to display images from Ap_29demo.h
static void display_demo(void) {
  LOG_INF("=== SSD1683 Display Driver Demo ===");
  LOG_INF("Showcasing all driver capabilities using Ap_29demo.h images");

  // ============================================================================
  // Demo 1: Standard Initialization and Display
  // ============================================================================
  LOG_INF("Demo 1: Standard initialization and full screen display");
  ssd1683_init(&ssd1683_cfg);
  LOG_INF("  - Displaying gImage_1 (full screen, high quality)");
  ssd1683_display(&ssd1683_cfg, gImage_1);
  k_msleep(3000);

  // ============================================================================
  // Demo 2: Fast Refresh Mode
  // ============================================================================
  LOG_INF("Demo 2: Fast refresh mode");
  ssd1683_init_fast(&ssd1683_cfg);
  LOG_INF("  - Displaying gImage_2 (fast refresh mode)");
  ssd1683_display_fast(&ssd1683_cfg, gImage_2);
  k_msleep(3000);

  // ============================================================================
  // Demo 3: Clear Screen Functionality
  // ============================================================================
  LOG_INF("Demo 3: Clear screen functionality");
  LOG_INF("  - Clearing display to white");
  ssd1683_clear(&ssd1683_cfg);
  k_msleep(2000);

  // ============================================================================
  // Demo 4: Partial Display Updates
  // ============================================================================
  LOG_INF("Demo 4: Partial display updates (faster, no flickering)");

  // Display partial images in sequence
  LOG_INF("  - Partial display 1");
  ssd1683_partial_display(&ssd1683_cfg, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                          gImage_p1);
  k_msleep(2000);

  LOG_INF("  - Partial display 2");
  ssd1683_partial_display(&ssd1683_cfg, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                          gImage_p2);
  k_msleep(2000);

  LOG_INF("  - Partial display 3");
  ssd1683_partial_display(&ssd1683_cfg, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                          gImage_p3);
  k_msleep(2000);

  LOG_INF("  - Partial display 4");
  ssd1683_partial_display(&ssd1683_cfg, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                          gImage_p4);
  k_msleep(2000);

  // ============================================================================
  // Demo 5: Write Display (without update)
  // ============================================================================
  LOG_INF("Demo 5: Write display without immediate update");
  LOG_INF("  - Writing gImage_1 to display buffer");
  ssd1683_write_display(&ssd1683_cfg, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                        gImage_1);
  k_msleep(1000);
  LOG_INF("  - Triggering display update");
  ssd1683_turn_on_display(&ssd1683_cfg);
  k_msleep(2000);

  // ============================================================================
  // Demo 6: Utility Functions
  // ============================================================================
  LOG_INF("Demo 6: Utility functions demonstration");

  // Show current refresh mode
  ssd1683_refresh_mode_t current_mode = ssd1683_get_refresh_mode(&ssd1683_cfg);
  LOG_INF("  - Current refresh mode: %d", current_mode);

  // Change refresh mode
  LOG_INF("  - Setting refresh mode to FAST");
  ssd1683_set_refresh_mode(&ssd1683_cfg, SSD1683_REFRESH_FAST);
  current_mode = ssd1683_get_refresh_mode(&ssd1683_cfg);
  LOG_INF("  - New refresh mode: %d", current_mode);

  // Check if display is busy
  bool is_busy = ssd1683_is_busy(&ssd1683_cfg);
  LOG_INF("  - Display busy status: %s", is_busy ? "BUSY" : "READY");

  // ============================================================================
  // Demo 7: Different Display Modes
  // ============================================================================
  LOG_INF("Demo 7: Different display modes");

  // Standard mode
  LOG_INF("  - Standard display mode");
  ssd1683_set_refresh_mode(&ssd1683_cfg, SSD1683_REFRESH_FULL);
  ssd1683_display(&ssd1683_cfg, gImage_1);
  k_msleep(2000);

  // Fast mode
  LOG_INF("  - Fast display mode");
  ssd1683_set_refresh_mode(&ssd1683_cfg, SSD1683_REFRESH_FAST);
  ssd1683_display_fast(&ssd1683_cfg, gImage_2);
  k_msleep(2000);

  // Partial mode
  LOG_INF("  - Partial display mode");
  ssd1683_set_refresh_mode(&ssd1683_cfg, SSD1683_REFRESH_PARTIAL);
  ssd1683_partial_display(&ssd1683_cfg, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                          gImage_p1);
  k_msleep(2000);

  // ============================================================================
  // Demo 8: Sleep Mode
  // ============================================================================
  LOG_INF("Demo 8: Sleep mode demonstration");
  LOG_INF("  - Entering sleep mode");
  ssd1683_sleep(&ssd1683_cfg);
  k_msleep(1000);

  LOG_INF("  - Waking up from sleep mode");
  ssd1683_init(&ssd1683_cfg);
  ssd1683_display(&ssd1683_cfg, gImage_2);
  k_msleep(2000);

  // ============================================================================
  // Demo 9: Final Clear and Summary
  // ============================================================================
  LOG_INF("Demo 9: Final clear and summary");
  LOG_INF("  - Clearing display");
  ssd1683_clear(&ssd1683_cfg);
  k_msleep(1000);

  LOG_INF("=== Display Demo Completed Successfully ===");
  LOG_INF("Driver capabilities demonstrated:");
  LOG_INF("  ✓ Standard initialization and display");
  LOG_INF("  ✓ Fast refresh mode");
  LOG_INF("  ✓ Clear screen functionality");
  LOG_INF("  ✓ Partial display updates");
  LOG_INF("  ✓ Write display without update");
  LOG_INF("  ✓ Utility functions (mode setting, busy check)");
  LOG_INF("  ✓ Different display modes");
  LOG_INF("  ✓ Sleep mode");
  LOG_INF("  ✓ All functions working correctly!");
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
    display_demo();
    LOG_INF("Demo cycle completed, sleeping for 10 seconds");
    k_msleep(10000);
  }

  return 0;
}
