#include "ssd1683.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ssd1683, LOG_LEVEL_INF);

// ============================================================================
// Forward Declarations
// ============================================================================
static void ssd1683_set_window(const struct ssd1683_config *cfg,
                               uint16_t x_start, uint16_t y_start,
                               uint16_t x_end, uint16_t y_end);
static void ssd1683_set_cursor(const struct ssd1683_config *cfg, uint16_t x,
                               uint16_t y);
static int ssd1683_init_common(const struct ssd1683_config *cfg);

// ============================================================================
// Helper Functions
// ============================================================================

// Calculate width in bytes for bitmap (aligned to 8 pixels)
static inline int ssd1683_calc_width_bytes(int width) {
  return (width + 7) / 8;
}

// Write image buffer to display RAM (BW or RED)
static void ssd1683_write_buffer_to_ram(const struct ssd1683_config *cfg,
                                        uint8_t cmd, const uint8_t *buffer,
                                        int width, int height) {
  int width_bytes = ssd1683_calc_width_bytes(width);
  ssd1683_write_cmd(cfg, cmd);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, buffer[i + j * width_bytes]);
    }
  }
}

// Wait for BUSY pin to go LOW
static void wait_busy(const struct ssd1683_config *cfg) {
  LOG_DBG("Waiting for BUSY pin...");
  while (gpio_pin_get_dt(&cfg->busy)) {
    k_msleep(10);
  }
  LOG_DBG("BUSY pin ready");
}

// ============================================================================
// Low-level SPI Commands
// ============================================================================

void ssd1683_write_cmd(const struct ssd1683_config *cfg, uint8_t cmd) {
  gpio_pin_set_dt(&cfg->dc, 0); // command mode
  struct spi_buf buf = {.buf = &cmd, .len = 1};
  struct spi_buf_set tx = {.buffers = &buf, .count = 1};
  int ret = spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
  if (ret < 0) {
    LOG_ERR("SPI write command failed: %d", ret);
  }
}

void ssd1683_write_data(const struct ssd1683_config *cfg, uint8_t data) {
  gpio_pin_set_dt(&cfg->dc, 1); // data mode
  struct spi_buf buf = {.buf = &data, .len = 1};
  struct spi_buf_set tx = {.buffers = &buf, .count = 1};
  int ret = spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
  if (ret < 0) {
    LOG_ERR("SPI write data failed: %d", ret);
  }
}

// ============================================================================
// Hardware Control Functions
// ============================================================================

void ssd1683_reset(const struct ssd1683_config *cfg) {
  gpio_pin_set_dt(&cfg->rst, 0);
  k_msleep(10);
  gpio_pin_set_dt(&cfg->rst, 1);
  k_msleep(10);
  LOG_INF("Hardware reset completed");
}

void ssd1683_deep_sleep(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_DEEP_SLEEP);
  ssd1683_write_data(cfg, 0x01);
  LOG_INF("Entered deep sleep mode");
}

// ============================================================================
// Initialization Functions
// ============================================================================

// Common initialization code shared by all init functions
static int ssd1683_init_common(const struct ssd1683_config *cfg) {
  // Check if all devices are ready
  if (!device_is_ready(cfg->spi_dev)) {
    LOG_ERR("SPI device not ready");
    return -ENODEV;
  }
  if (!device_is_ready(cfg->dc.port)) {
    LOG_ERR("DC GPIO port not ready");
    return -ENODEV;
  }
  if (!device_is_ready(cfg->rst.port)) {
    LOG_ERR("RST GPIO port not ready");
    return -ENODEV;
  }
  if (!device_is_ready(cfg->busy.port)) {
    LOG_ERR("BUSY GPIO port not ready");
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

  // Perform hardware reset
  ssd1683_reset(cfg);
  wait_busy(cfg);

  // Soft reset
  ssd1683_write_cmd(cfg, SSD1683_CMD_SOFT_RESET);
  wait_busy(cfg);

  // Set window to full display
  ssd1683_set_window(cfg, 0, 0, cfg->width - 1, cfg->height - 1);

  // Set cursor to origin
  ssd1683_set_cursor(cfg, 0, 0);
  wait_busy(cfg);

  return 0;
}

// Standard initialization (full quality)
int ssd1683_init(const struct ssd1683_config *cfg) {
  int ret = ssd1683_init_common(cfg);
  if (ret < 0) {
    return ret;
  }

  // Display update control
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL);
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);

  // Border waveform
  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x10);

  // Data entry mode (increment X and Y)
  ssd1683_write_cmd(cfg, SSD1683_CMD_DATA_ENTRY_MODE);
  ssd1683_write_data(cfg, 0x03);

  LOG_INF("SSD1683 initialized (standard mode)");
  return 0;
}

// Fast initialization (reduced quality, faster refresh)
int ssd1683_init_fast(const struct ssd1683_config *cfg) {
  int ret = ssd1683_init_common(cfg);
  if (ret < 0) {
    return ret;
  }

  // Display update control
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL);
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);

  // Border waveform (fast mode)
  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x05);

  // Write temperature value
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_TEMP);
  ssd1683_write_data(cfg, 0x5A); // 25 degrees C

  // Load temperature value
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_SEQ);
  ssd1683_write_data(cfg, 0x91);
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  wait_busy(cfg);

  // Data entry mode (increment X and Y)
  ssd1683_write_cmd(cfg, SSD1683_CMD_DATA_ENTRY_MODE);
  ssd1683_write_data(cfg, 0x03);

  LOG_INF("SSD1683 initialized (fast mode)");
  return 0;
}

// ============================================================================
// Display Control Functions
// ============================================================================

// Clear display to white (set all pixels to 1)
void ssd1683_clear(const struct ssd1683_config *cfg) {
  int width_bytes = ssd1683_calc_width_bytes(cfg->width);
  int total_bytes = width_bytes * cfg->height;

  // Clear BW RAM (0xFF = white)
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM_BW);
  for (int i = 0; i < total_bytes; i++) {
    ssd1683_write_data(cfg, 0xFF);
  }

  // Clear RED RAM (0xFF = no red) - only done here to disable red channel
  // This ensures monochrome operation for all subsequent operations
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM_RED);
  for (int i = 0; i < total_bytes; i++) {
    ssd1683_write_data(cfg, 0xFF);
  }

  LOG_INF("Display cleared (monochrome mode)");
}

// Standard refresh (full update, high quality)
void ssd1683_refresh(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_SEQ);
  ssd1683_write_data(cfg, 0xF7);
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  wait_busy(cfg);
  LOG_DBG("Display refreshed (standard)");
}

// Fast refresh (partial waveform, lower quality)
void ssd1683_refresh_fast(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_SEQ);
  ssd1683_write_data(cfg, 0xC7);
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  wait_busy(cfg);
  LOG_DBG("Display refreshed (fast)");
}

// Partial refresh (for partial screen updates)
void ssd1683_refresh_partial(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_SEQ);
  ssd1683_write_data(cfg, 0xFF);
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  wait_busy(cfg);
  LOG_DBG("Display refreshed (partial)");
}

// ============================================================================
// Buffer Operations (Monochrome Mode)
// ============================================================================
// NOTE: This driver operates in MONOCHROME mode only.
// All buffer operations write to BW RAM only. RED RAM is cleared during
// initialization/clear to ensure no red pixels interfere.

// Flush framebuffer to BW RAM and refresh display
void ssd1683_flush(const struct ssd1683_config *cfg, uint8_t *image_buffer) {
  // Set window to full screen
  ssd1683_set_window(cfg, 0, 0, cfg->width - 1, cfg->height - 1);
  ssd1683_set_cursor(cfg, 0, 0);

  // Write buffer to BW RAM only
  ssd1683_write_buffer_to_ram(cfg, SSD1683_CMD_WRITE_RAM_BW, image_buffer,
                              cfg->width, cfg->height);

  // Auto-refresh after flush
  ssd1683_refresh(cfg);
  LOG_DBG("Flushed framebuffer to display");
}

// ============================================================================
// Advanced Display Functions (Monochrome Mode)
// ============================================================================

// Fast display (write and refresh with fast mode)
void ssd1683_display_fast(const struct ssd1683_config *cfg, uint8_t *image) {
  ssd1683_set_window(cfg, 0, 0, cfg->width - 1, cfg->height - 1);

  // Write buffer to BW RAM only
  ssd1683_write_buffer_to_ram(cfg, SSD1683_CMD_WRITE_RAM_BW, image, cfg->width,
                              cfg->height);

  ssd1683_refresh_fast(cfg);
  LOG_DBG("Fast display completed");
}

// Partial display update (update only a region)
void ssd1683_partial_display(const struct ssd1683_config *cfg, uint16_t x,
                             uint16_t y, uint16_t w, uint16_t l,
                             uint8_t *image) {
  // Configure border waveform for partial update
  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x80);

  // Display update control for partial
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE_CTRL);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x80);

  // Set window and cursor to target region
  ssd1683_set_window(cfg, x, y, x + w - 1, y + l - 1);
  ssd1683_set_cursor(cfg, x, y);

  // Write buffer to BW RAM only
  ssd1683_write_buffer_to_ram(cfg, SSD1683_CMD_WRITE_RAM_BW, image, w, l);

  ssd1683_refresh_partial(cfg);
  LOG_DBG("Partial display update completed");
}

// Write display without refreshing (prepare buffer for later refresh)
void ssd1683_write_display(const struct ssd1683_config *cfg, uint16_t x,
                           uint16_t y, uint16_t w, uint16_t l, uint8_t *image) {
  ssd1683_set_window(cfg, x, y, x + w - 1, y + l - 1);
  ssd1683_set_cursor(cfg, x, y);

  // Write to BW RAM only
  ssd1683_write_buffer_to_ram(cfg, SSD1683_CMD_WRITE_RAM_BW, image, w, l);

  LOG_DBG("Write display buffer completed (no refresh)");
}

// ============================================================================
// Internal Window/Cursor Management
// ============================================================================

// Set drawing window (defines active area)
static void ssd1683_set_window(const struct ssd1683_config *cfg,
                               uint16_t x_start, uint16_t y_start,
                               uint16_t x_end, uint16_t y_end) {
  // Set RAM X address start/end (in bytes, divide by 8)
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X_ADDR);
  ssd1683_write_data(cfg, (x_start >> 3) & 0xFF);
  ssd1683_write_data(cfg, (x_end >> 3) & 0xFF);

  // Set RAM Y address start/end (in pixels)
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y_ADDR);
  ssd1683_write_data(cfg, y_start & 0xFF);
  ssd1683_write_data(cfg, (y_start >> 8) & 0xFF);
  ssd1683_write_data(cfg, y_end & 0xFF);
  ssd1683_write_data(cfg, (y_end >> 8) & 0xFF);
}

// Set cursor position (starting point for writes)
static void ssd1683_set_cursor(const struct ssd1683_config *cfg, uint16_t x,
                               uint16_t y) {
  // Set RAM X address counter (in bytes, divide by 8)
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X_COUNTER);
  ssd1683_write_data(cfg, (x >> 3) & 0xFF);

  // Set RAM Y address counter (in pixels)
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y_COUNTER);
  ssd1683_write_data(cfg, y & 0xFF);
  ssd1683_write_data(cfg, (y >> 8) & 0xFF);
}

// ============================================================================
// Drawing Helper Functions (Application-level)
// ============================================================================

// Draw vertical line on buffer
void ssd1683_draw_vline(uint8_t *image, int x, int y_start, int y_end,
                        int width, int height) {
  // Validate X coordinate
  if (x < 0 || x >= width) {
    return;
  }

  // Clamp Y coordinates
  if (y_start < 0)
    y_start = 0;
  if (y_end >= height)
    y_end = height - 1;

  int width_bytes = ssd1683_calc_width_bytes(width);

  for (int y = y_start; y <= y_end; y++) {
    int byte_index = (x / 8) + y * width_bytes;
    uint8_t mask = 1 << (7 - (x % 8));
    image[byte_index] &= ~mask; // Clear bit = black pixel
  }
}

// Draw horizontal line on buffer
void ssd1683_draw_hline(uint8_t *image, int x_start, int x_end, int y,
                        int width, int height) {
  // Validate Y coordinate
  if (y < 0 || y >= height) {
    return;
  }

  // Clamp X coordinates
  if (x_start < 0)
    x_start = 0;
  if (x_end >= width)
    x_end = width - 1;

  int width_bytes = ssd1683_calc_width_bytes(width);

  for (int x = x_start; x <= x_end; x++) {
    int byte_index = (x / 8) + y * width_bytes;
    uint8_t mask = 1 << (7 - (x % 8));
    image[byte_index] &= ~mask; // Clear bit = black pixel
  }
}
