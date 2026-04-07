#ifndef SSD1683_H
#define SSD1683_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

/**
 * @brief SSD1683 E-Paper Display Driver
 *
 * This driver provides a clean Zephyr-style interface for the SSD1683
 * e-paper display controller. It supports 400x300 pixel displays with
 * both full and partial update capabilities.
 */

// Display dimensions
#define SSD1683_WIDTH 400
#define SSD1683_HEIGHT 300
#define SSD1683_WIDTH_VISIBLE SSD1683_WIDTH

// Timing constants (in ms)
#define SSD1683_POWER_ON_TIME 100
#define SSD1683_POWER_OFF_TIME 300
#define SSD1683_FULL_REFRESH_TIME 1200
#define SSD1683_PARTIAL_REFRESH_TIME 400

// Command definitions
#define SSD1683_CMD_SWRESET 0x12
#define SSD1683_CMD_DEEP_SLEEP 0x10
#define SSD1683_CMD_SET_RAM_X 0x44
#define SSD1683_CMD_SET_RAM_Y 0x45
#define SSD1683_CMD_SET_RAM_X_COUNTER 0x4E
#define SSD1683_CMD_SET_RAM_Y_COUNTER 0x4F
#define SSD1683_CMD_WRITE_RAM_CURRENT 0x24 // Current buffer (what you see now)
#define SSD1683_CMD_WRITE_RAM_PREVIOUS                                         \
  0x26 // Previous buffer (what was there before)
#define SSD1683_CMD_DISPLAY_UPDATE 0x20
#define SSD1683_CMD_DISPLAY_UPDATE_CTRL 0x21
#define SSD1683_CMD_POWER_OFF 0x22

/**
 * @brief SSD1683 driver configuration structure
 *
 * Contains hardware-specific configuration including SPI bus,
 * GPIO pins, and display dimensions.
 */
struct ssd1683_config {
  struct spi_dt_spec bus;   /**< SPI bus specification */
  struct gpio_dt_spec dc;   /**< Data/Command GPIO pin */
  struct gpio_dt_spec rst;  /**< Reset GPIO pin */
  struct gpio_dt_spec busy; /**< Busy GPIO pin */
  uint16_t width;           /**< Display width in pixels */
  uint16_t height;          /**< Display height in pixels */
};

/**
 * @brief SSD1683 driver data structure
 *
 * Contains runtime state information for the driver instance.
 * Matches reference implementation state tracking.
 */
struct ssd1683_data {
  bool is_powered_on;  /**< Power state flag */
  bool is_initialized; /**< Initialization state flag (like _init_display_done)
                        */
  bool is_first_write; /**< First write flag (like _initial_write) */
  bool is_first_refresh; /**< First refresh flag (like _initial_refresh) */
  bool use_fast_update;  /**< Fast update mode flag (like _use_fast_update) */
  bool is_hibernating;   /**< Hibernation state flag (like _hibernating) */
  uint32_t last_update_time; /**< Timestamp of last update */
};

/**
 * @brief Initialize the SSD1683 display
 *
 * @param dev Device pointer (for future Zephyr driver API compatibility)
 * @param cfg Configuration structure
 * @return 0 on success, negative error code on failure
 */
int ssd1683_init(const struct device *dev, const struct ssd1683_config *cfg);

/**
 * @brief Power on the display
 *
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
int ssd1683_power_on(const struct device *dev);

/**
 * @brief Power off the display
 *
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
int ssd1683_power_off(const struct device *dev);

/**
 * @brief Put display into hibernate mode
 *
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
int ssd1683_hibernate(const struct device *dev);

/**
 * @brief Clear the entire screen (like reference clearScreen)
 * Sets both previous and current buffers, then does full refresh
 *
 * @param dev Device pointer
 * @param value Fill value (0x00 = black, 0xFF = white)
 * @return 0 on success, negative error code on failure
 */
int ssd1683_clear_screen(const struct device *dev, uint8_t value);

/**
 * @brief Write screen buffer (like reference writeScreenBuffer)
 * Only sets current buffer (0x24)
 *
 * @param dev Device pointer
 * @param value Fill value (0x00 = black, 0xFF = white)
 * @return 0 on success, negative error code on failure
 */
int ssd1683_write_screen_buffer(const struct device *dev, uint8_t value);

/**
 * @brief Write screen buffer again (like reference writeScreenBufferAgain)
 * Sets both current and previous buffers for differential update
 *
 * @param dev Device pointer
 * @param value Fill value (0x00 = black, 0xFF = white)
 * @return 0 on success, negative error code on failure
 */
int ssd1683_write_screen_buffer_again(const struct device *dev, uint8_t value);

/**
 * @brief Write image data to display memory (CURRENT buffer only)
 *
 * This writes the image to the CURRENT buffer (0x24). After a partial refresh,
 * you should call ssd1683_write_image_again() to synchronize the PREVIOUS
 * buffer.
 *
 * @param dev Device pointer
 * @param bitmap Image data buffer
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width in pixels
 * @param h Height in pixels
 * @param invert Invert image data
 * @param mirror_y Mirror image vertically
 * @return 0 on success, negative error code on failure
 */
int ssd1683_write_image(const struct device *dev, const uint8_t *bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                        bool mirror_y);

/**
 * @brief Write image data to BOTH display buffers (for differential updates)
 *
 * This is critical for partial refresh to work correctly. After a partial
 * refresh, call this function to synchronize the PREVIOUS buffer (0x26) with
 * the CURRENT buffer (0x24). This ensures the next partial refresh compares
 * against the correct previous state.
 *
 * Like the reference GxEPD2 library's writeImageAgain() function.
 *
 * @param dev Device pointer
 * @param bitmap Image data buffer
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width in pixels
 * @param h Height in pixels
 * @param invert Invert image data
 * @param mirror_y Mirror image vertically
 * @return 0 on success, negative error code on failure
 */
int ssd1683_write_image_again(const struct device *dev, const uint8_t *bitmap,
                              int16_t x, int16_t y, int16_t w, int16_t h,
                              bool invert, bool mirror_y);

/**
 * @brief Refresh the display
 *
 * @param dev Device pointer
 * @param partial True for partial update, false for full update
 * @return 0 on success, negative error code on failure
 */
int ssd1683_refresh(const struct device *dev, bool partial);

/**
 * @brief Set fast update mode
 *
 * @param dev Device pointer
 * @param fast_update Enable fast update mode
 * @return 0 on success, negative error code on failure
 */
int ssd1683_set_fast_update(const struct device *dev, bool fast_update);

/**
 * @brief Get current power state
 *
 * @param dev Device pointer
 * @return true if powered on, false otherwise
 */
bool ssd1683_is_powered_on(const struct device *dev);

/**
 * @brief Get initialization state
 *
 * @param dev Device pointer
 * @return true if initialized, false otherwise
 */
bool ssd1683_is_initialized(const struct device *dev);

#endif // SSD1683_H