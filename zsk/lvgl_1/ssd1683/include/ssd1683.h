#ifndef SSD1683_H
#define SSD1683_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

// Display resolution constants
#define SSD1683_WIDTH 400
#define SSD1683_HEIGHT 300

// Refresh modes
typedef enum {
  SSD1683_REFRESH_FULL = 0,    // Full refresh (high quality)
  SSD1683_REFRESH_PARTIAL = 1, // Partial refresh (faster)
  SSD1683_REFRESH_FAST = 2     // Fast refresh (fastest)
} ssd1683_refresh_mode_t;

// Color definitions
typedef enum {
  SSD1683_COLOR_WHITE = 0,
  SSD1683_COLOR_BLACK = 1,
  SSD1683_COLOR_LIGHT_GRAY = 2,
  SSD1683_COLOR_DARK_GRAY = 3
} ssd1683_color_t;

struct ssd1683_config {
  struct spi_dt_spec bus; // Modern Zephyr: combines device + config
  struct gpio_dt_spec dc;
  struct gpio_dt_spec rst;
  struct gpio_dt_spec busy;
  uint16_t width;
  uint16_t height;
};

// ============================================================================
// Function Declarations (matching reference driver)
// ============================================================================

// Initialization functions
void ssd1683_init(const struct ssd1683_config *cfg);
void ssd1683_init_fast(const struct ssd1683_config *cfg);
void ssd1683_init_4gray(const struct ssd1683_config *cfg);

// Display functions
void ssd1683_clear(const struct ssd1683_config *cfg);
void ssd1683_display(const struct ssd1683_config *cfg, uint8_t *image);
void ssd1683_display_fast(const struct ssd1683_config *cfg, uint8_t *image);
void ssd1683_display_4gray(const struct ssd1683_config *cfg, uint8_t *image);
void ssd1683_partial_display(const struct ssd1683_config *cfg, uint16_t x,
                             uint16_t y, uint16_t w, uint16_t h,
                             uint8_t *image);
void ssd1683_sleep(const struct ssd1683_config *cfg);
void ssd1683_write_display(const struct ssd1683_config *cfg, uint16_t x,
                           uint16_t y, uint16_t w, uint16_t h, uint8_t *image);
void ssd1683_turn_on_display_fast(const struct ssd1683_config *cfg);
void ssd1683_turn_on_display(const struct ssd1683_config *cfg);
void ssd1683_turn_on_display_partial(const struct ssd1683_config *cfg);

// Utility functions
void ssd1683_set_refresh_mode(const struct ssd1683_config *cfg,
                              ssd1683_refresh_mode_t mode);
ssd1683_refresh_mode_t
ssd1683_get_refresh_mode(const struct ssd1683_config *cfg);
bool ssd1683_is_busy(const struct ssd1683_config *cfg);

#endif // SSD1683_H
