/**
 * @file ssd1683.c
 * @brief SSD1683 E-Paper Display Driver Implementation
 *
 * This file implements a clean Zephyr-style driver for the SSD1683
 * e-paper display controller with proper state management and error handling.
 */

#include "ssd1683.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(ssd1683, LOG_LEVEL_INF);

// ============================================================================
// Section 1: Low-level SPI Communication Helpers
// ============================================================================

/**
 * @brief Wait for BUSY pin to go LOW
 *
 * @param cfg Configuration structure
 * @return 0 on success, -ETIMEDOUT on timeout
 */
static int _ssd1683_wait_busy(const struct ssd1683_config *cfg) {
  LOG_DBG("Waiting for BUSY pin...");

  while (gpio_pin_get_dt(&cfg->busy)) {
    k_msleep(10);
  }
  LOG_DBG("BUSY pin ready");
  return 0;
}

/**
 * @brief Write command to display
 *
 * @param cfg Configuration structure
 * @param cmd Command byte
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_write_cmd(const struct ssd1683_config *cfg, uint8_t cmd) {
  int ret;

  ret = gpio_pin_set_dt(&cfg->dc, 0); // command mode
  if (ret < 0) {
    LOG_ERR("Failed to set DC pin: %d", ret);
    return ret;
  }

  struct spi_buf buf = {.buf = &cmd, .len = 1};
  struct spi_buf_set tx = {.buffers = &buf, .count = 1};

  ret = spi_write_dt(&cfg->bus, &tx);
  if (ret < 0) {
    LOG_ERR("SPI write command failed: %d", ret);
    return ret;
  }

  return 0;
}

/**
 * @brief Write data to display
 *
 * @param cfg Configuration structure
 * @param data Data byte
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_write_data(const struct ssd1683_config *cfg, uint8_t data) {
  int ret;

  ret = gpio_pin_set_dt(&cfg->dc, 1); // data mode
  if (ret < 0) {
    LOG_ERR("Failed to set DC pin: %d", ret);
    return ret;
  }

  struct spi_buf buf = {.buf = &data, .len = 1};
  struct spi_buf_set tx = {.buffers = &buf, .count = 1};

  ret = spi_write_dt(&cfg->bus, &tx);
  if (ret < 0) {
    LOG_ERR("SPI write data failed: %d", ret);
    return ret;
  }

  return 0;
}

// ============================================================================
// Section 2: Hardware Control Functions
// ============================================================================

/**
 * @brief Reset the display hardware
 *
 * @param cfg Configuration structure
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_reset(const struct ssd1683_config *cfg) {
  gpio_pin_set_dt(&cfg->rst, 0);
  k_msleep(10);
  gpio_pin_set_dt(&cfg->rst, 1);
  k_msleep(10);
  LOG_INF("Hardware reset completed");
  return 0;
}

// ============================================================================
// Section 3: Display Initialization
// ============================================================================

/**
 * @brief Set partial RAM area for display updates
 *
 * @param cfg Configuration structure
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_set_partial_ram_area(const struct ssd1683_config *cfg,
                                         uint16_t x, uint16_t y, uint16_t w,
                                         uint16_t h) {
  int ret;

  ret = _ssd1683_write_cmd(cfg, 0x11); // set ram entry mode
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x03); // x increase, y increase : normal mode
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, 0x44);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, x / 8);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, (x + w - 1) / 8);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, 0x45);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, y % 256);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, y / 256);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, (y + h - 1) % 256);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, (y + h - 1) / 256);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, 0x4e);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, x / 8);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, 0x4f);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, y % 256);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, y / 256);
  if (ret < 0)
    return ret;

  return 0;
}

/**
 * @brief Initialize display with proper sequence (like reference _InitDisplay)
 *
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_init_display(const struct device *dev) {
  const struct ssd1683_config *cfg = dev->config;
  struct ssd1683_data *data = dev->data;
  int ret;

  if (data->is_initialized) {
    LOG_DBG("Display already initialized");
    return 0;
  }

  // Reset if hibernating (like reference)
  if (data->is_hibernating) {
    ret = _ssd1683_reset(cfg);
    if (ret < 0)
      return ret;
  }

  k_msleep(10); // 10ms according to specs (like reference)

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_SWRESET);
  if (ret < 0)
    return ret;

  k_msleep(10); // 10ms according to specs (like reference)

  // Set MUX as 300 (like reference)
  ret = _ssd1683_write_cmd(cfg, 0x01);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x2B);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x01);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x00);
  if (ret < 0)
    return ret;

  // BorderWavefrom (like reference)
  ret = _ssd1683_write_cmd(cfg, 0x3C);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x01);
  if (ret < 0)
    return ret;

  // Read built-in temperature sensor (like reference)
  ret = _ssd1683_write_cmd(cfg, 0x18);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x80);
  if (ret < 0)
    return ret;

  ret = _ssd1683_set_partial_ram_area(cfg, 0, 0, cfg->width, cfg->height);
  if (ret < 0)
    return ret;

  data->is_initialized = true;
  data->is_hibernating = false;
  LOG_INF("Display initialization completed");
  return 0;
}

// ============================================================================
// Section 4: Display Memory Functions
// ============================================================================

/**
 * @brief Write screen buffer with specified value (like reference
 * _writeScreenBuffer) Supports both current (0x24) and previous (0x26) buffers
 *
 * @param dev Device pointer
 * @param command Buffer command (0x24 for current, 0x26 for previous)
 * @param value Fill value
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_write_screen_buffer(const struct device *dev,
                                        uint8_t command, uint8_t value) {
  const struct ssd1683_config *cfg = dev->config;
  struct ssd1683_data *data = dev->data;
  int ret;

  if (!data->is_initialized) {
    ret = _ssd1683_init_display(dev);
    if (ret < 0)
      return ret;
  }

  ret = _ssd1683_set_partial_ram_area(cfg, 0, 0, cfg->width, cfg->height);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, command);
  if (ret < 0)
    return ret;

  // Fill screen with value
  uint32_t total_bytes = (uint32_t)cfg->width * (uint32_t)cfg->height / 8;
  for (uint32_t i = 0; i < total_bytes; i++) {
    ret = _ssd1683_write_data(cfg, value);
    if (ret < 0)
      return ret;
  }

  return 0;
}

/**
 * @brief Update display with full refresh (like reference _Update_Full)
 *
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_update_full(const struct device *dev) {
  const struct ssd1683_config *cfg = dev->config;
  struct ssd1683_data *data = dev->data;
  int ret;

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x40); // bypass RED as 0
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x00); // single chip application
  if (ret < 0)
    return ret;

  if (data->use_fast_update) {
    ret = _ssd1683_write_cmd(cfg, 0x1A); // Write to temperature register
    if (ret < 0)
      return ret;

    ret = _ssd1683_write_data(cfg, 0x6E); // 2024 version, ok for 2023 version
    if (ret < 0)
      return ret;

    ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_POWER_OFF);
    if (ret < 0)
      return ret;

    ret = _ssd1683_write_data(cfg, 0xd7);
    if (ret < 0)
      return ret;
  } else {
    ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_POWER_OFF);
    if (ret < 0)
      return ret;

    ret = _ssd1683_write_data(cfg, 0xf7);
    if (ret < 0)
      return ret;
  }

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  if (ret < 0)
    return ret;

  ret = _ssd1683_wait_busy(cfg);
  if (ret < 0)
    return ret;

  data->is_powered_on = false;
  data->is_first_refresh = false;
  LOG_DBG("Full update completed");
  return 0;
}

/**
 * @brief Update display with partial refresh (like reference _Update_Part)
 *
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_update_partial(const struct device *dev) {
  const struct ssd1683_config *cfg = dev->config;
  struct ssd1683_data *data = dev->data;
  int ret;

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x00); // RED normal
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x00); // single chip application
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_POWER_OFF);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0xfc);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  if (ret < 0)
    return ret;

  ret = _ssd1683_wait_busy(cfg);
  if (ret < 0)
    return ret;

  data->is_powered_on = true; // Partial updates keep power on
  LOG_DBG("Partial update completed");
  return 0;
}

// ============================================================================
// Section 5: Power Management
// ============================================================================

/**
 * @brief Power on the display
 *
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_power_on(const struct device *dev) {
  const struct ssd1683_config *cfg = dev->config;
  struct ssd1683_data *data = dev->data;
  int ret;

  if (data->is_powered_on) {
    LOG_DBG("Display already powered on");
    return 0;
  }

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_POWER_OFF);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0xe0);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  if (ret < 0)
    return ret;

  ret = _ssd1683_wait_busy(cfg);
  if (ret < 0)
    return ret;

  data->is_powered_on = true;
  LOG_DBG("Power on completed");
  return 0;
}

/**
 * @brief Power off the display
 *
 * @param dev Device pointer
 * @return 0 on success, negative error code on failure
 */
static int _ssd1683_power_off(const struct device *dev) {
  const struct ssd1683_config *cfg = dev->config;
  struct ssd1683_data *data = dev->data;
  int ret;

  if (!data->is_powered_on) {
    LOG_DBG("Display already powered off");
    return 0;
  }

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_POWER_OFF);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x83);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  if (ret < 0)
    return ret;

  ret = _ssd1683_wait_busy(cfg);
  if (ret < 0)
    return ret;

  data->is_powered_on = false;
  LOG_DBG("Power off completed");
  return 0;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int ssd1683_init(const struct device *dev, const struct ssd1683_config *cfg) {
  struct ssd1683_data *data = dev->data;

  if (!cfg) {
    LOG_ERR("Configuration is NULL");
    return -EINVAL;
  }

  // Check if all devices are ready
  if (!spi_is_ready_dt(&cfg->bus)) {
    LOG_ERR("SPI bus not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&cfg->dc)) {
    LOG_ERR("DC GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&cfg->rst)) {
    LOG_ERR("RST GPIO not ready");
    return -ENODEV;
  }
  if (!gpio_is_ready_dt(&cfg->busy)) {
    LOG_ERR("BUSY GPIO not ready");
    return -ENODEV;
  }

  // Configure GPIO pins
  int ret;
  ret = gpio_pin_configure_dt(&cfg->dc, GPIO_OUTPUT_ACTIVE);
  if (ret < 0) {
    LOG_ERR("Failed to configure DC pin: %d", ret);
    return ret;
  }
  ret = gpio_pin_configure_dt(&cfg->rst, GPIO_OUTPUT_ACTIVE);
  if (ret < 0) {
    LOG_ERR("Failed to configure RST pin: %d", ret);
    return ret;
  }
  ret = gpio_pin_configure_dt(&cfg->busy, GPIO_INPUT);
  if (ret < 0) {
    LOG_ERR("Failed to configure BUSY pin: %d", ret);
    return ret;
  }

  // Initialize data structure (like reference)
  data->is_powered_on = false;
  data->is_initialized = false;
  data->is_first_write = true;   // like _initial_write
  data->is_first_refresh = true; // like _initial_refresh
  data->use_fast_update = true;  // like _use_fast_update
  data->is_hibernating = false;  // like _hibernating
  data->last_update_time = 0;

  LOG_INF("SSD1683 driver initialized");
  return 0;
}

int ssd1683_power_on(const struct device *dev) {
  if (!dev) {
    return -EINVAL;
  }

  return _ssd1683_power_on(dev);
}

int ssd1683_power_off(const struct device *dev) {
  if (!dev) {
    return -EINVAL;
  }

  return _ssd1683_power_off(dev);
}

int ssd1683_hibernate(const struct device *dev) {
  const struct ssd1683_config *cfg = dev->config;
  struct ssd1683_data *data = dev->data;
  int ret;

  if (!dev) {
    return -EINVAL;
  }

  ret = _ssd1683_power_off(dev);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_DEEP_SLEEP);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_data(cfg, 0x1); // enter deep sleep
  if (ret < 0)
    return ret;

  data->is_hibernating = true;
  data->is_initialized = false;
  LOG_INF("Hibernate completed");
  return 0;
}

int ssd1683_clear_screen(const struct device *dev, uint8_t value) {
  struct ssd1683_data *data = dev->data;
  int ret;

  if (!dev) {
    return -EINVAL;
  }

  // Like reference clearScreen: set both previous and current buffers
  ret =
      _ssd1683_write_screen_buffer(dev, SSD1683_CMD_WRITE_RAM_PREVIOUS, value);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_screen_buffer(dev, SSD1683_CMD_WRITE_RAM_CURRENT, value);
  if (ret < 0)
    return ret;

  ret = ssd1683_refresh(dev, false);
  if (ret < 0)
    return ret;

  data->is_first_write = false;
  LOG_DBG("Screen cleared with value: 0x%02X", value);
  return 0;
}

int ssd1683_write_screen_buffer(const struct device *dev, uint8_t value) {
  struct ssd1683_data *data = dev->data;
  int ret;

  if (!dev) {
    return -EINVAL;
  }

  // Like reference writeScreenBuffer: only set current buffer
  if (data->is_first_write) {
    return ssd1683_clear_screen(dev, value);
  }

  ret = _ssd1683_write_screen_buffer(dev, SSD1683_CMD_WRITE_RAM_CURRENT, value);
  if (ret < 0)
    return ret;

  LOG_DBG("Screen buffer written with value: 0x%02X", value);
  return 0;
}

int ssd1683_write_screen_buffer_again(const struct device *dev, uint8_t value) {
  int ret;

  if (!dev) {
    return -EINVAL;
  }

  // Like reference writeScreenBufferAgain: set both current and previous
  ret = _ssd1683_write_screen_buffer(dev, SSD1683_CMD_WRITE_RAM_CURRENT, value);
  if (ret < 0)
    return ret;

  ret =
      _ssd1683_write_screen_buffer(dev, SSD1683_CMD_WRITE_RAM_PREVIOUS, value);
  if (ret < 0)
    return ret;

  LOG_DBG("Screen buffer written again with value: 0x%02X", value);
  return 0;
}

int ssd1683_write_image(const struct device *dev, const uint8_t *bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert,
                        bool mirror_y) {
  const struct ssd1683_config *cfg = dev->config;
  struct ssd1683_data *data = dev->data;
  int ret;

  if (!dev || !bitmap) {
    return -EINVAL;
  }

  if (!data->is_initialized) {
    ret = _ssd1683_init_display(dev);
    if (ret < 0)
      return ret;
  }

  if (data->is_first_write) {
    ret =
        _ssd1683_write_screen_buffer(dev, SSD1683_CMD_WRITE_RAM_CURRENT,
                                     0xFF); // initial full screen buffer clean
    if (ret < 0)
      return ret;
  }

  int16_t wb = (w + 7) / 8;   // width bytes, bitmaps are padded
  x -= x % 8;                 // byte boundary
  w = wb * 8;                 // byte boundary
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 =
      x + w < (int16_t)cfg->width ? w : (int16_t)cfg->width - x; // limit
  int16_t h1 =
      y + h < (int16_t)cfg->height ? h : (int16_t)cfg->height - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;

  if ((w1 <= 0) || (h1 <= 0)) {
    LOG_WRN("Invalid image dimensions");
    return -EINVAL;
  }

  ret = _ssd1683_set_partial_ram_area(cfg, x1, y1, w1, h1);
  if (ret < 0)
    return ret;

  ret = _ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM_CURRENT);
  if (ret < 0)
    return ret;

  // Write image data
  for (int16_t i = 0; i < h1; i++) {
    for (int16_t j = 0; j < w1 / 8; j++) {
      uint8_t data_byte;
      // use wb, h of bitmap for index!
      int16_t idx = mirror_y ? j + dx / 8 + ((h - 1 - (i + dy))) * wb
                             : j + dx / 8 + (i + dy) * wb;
      data_byte = bitmap[idx];
      if (invert) {
        data_byte = ~data_byte;
      }
      ret = _ssd1683_write_data(cfg, data_byte);
      if (ret < 0)
        return ret;
    }
  }

  return 0;
}

int ssd1683_refresh(const struct device *dev, bool partial) {
  if (!dev) {
    return -EINVAL;
  }

  // Like reference refresh logic
  if (partial) {
    return _ssd1683_update_partial(dev);
  } else {
    return _ssd1683_update_full(dev);
  }
}

int ssd1683_set_fast_update(const struct device *dev, bool fast_update) {
  struct ssd1683_data *data = dev->data;

  if (!dev) {
    return -EINVAL;
  }

  data->use_fast_update = fast_update;
  LOG_DBG("Fast update set to: %s", fast_update ? "true" : "false");
  return 0;
}

bool ssd1683_is_powered_on(const struct device *dev) {
  if (!dev) {
    return false;
  }

  struct ssd1683_data *data = dev->data;
  return data->is_powered_on;
}

bool ssd1683_is_initialized(const struct device *dev) {
  if (!dev) {
    return false;
  }

  struct ssd1683_data *data = dev->data;
  return data->is_initialized;
}