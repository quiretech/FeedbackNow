#include "ssd1683.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ssd1683, LOG_LEVEL_INF);

// ============================================================================
// Forward Declarations
// ============================================================================
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
  wait_busy(cfg);
  gpio_pin_set_dt(&cfg->dc, 0); // command mode
  struct spi_buf buf = {.buf = &cmd, .len = 1};
  struct spi_buf_set tx = {.buffers = &buf, .count = 1};
  int ret = spi_write_dt(&cfg->bus, &tx);
  if (ret < 0) {
    LOG_ERR("SPI write command failed: %d", ret);
  }
}

void ssd1683_write_data(const struct ssd1683_config *cfg, uint8_t data) {
  gpio_pin_set_dt(&cfg->dc, 1); // data mode
  struct spi_buf buf = {.buf = &data, .len = 1};
  struct spi_buf_set tx = {.buffers = &buf, .count = 1};
  int ret = spi_write_dt(&cfg->bus, &tx);
  if (ret < 0) {
    LOG_ERR("SPI write data failed: %d", ret);
  }
}

// Write command followed by a buffer of data (efficient bulk write)
// This avoids toggling DC pin for every byte
void ssd1683_write_cmd_buffer(const struct ssd1683_config *cfg, uint8_t cmd,
                              const uint8_t *data, size_t len) {
  // Wait for display to be ready before sending command
  wait_busy(cfg);

  // Write command
  gpio_pin_set_dt(&cfg->dc, 0); // command mode
  struct spi_buf cmd_buf = {.buf = &cmd, .len = 1};
  struct spi_buf_set cmd_tx = {.buffers = &cmd_buf, .count = 1};
  int ret = spi_write_dt(&cfg->bus, &cmd_tx);
  if (ret < 0) {
    LOG_ERR("SPI write command failed: %d", ret);
    return;
  }

  // Write data buffer (if any)
  if (data && len > 0) {
    gpio_pin_set_dt(&cfg->dc, 1); // data mode
    struct spi_buf data_buf = {.buf = (void *)data, .len = len};
    struct spi_buf_set data_tx = {.buffers = &data_buf, .count = 1};
    ret = spi_write_dt(&cfg->bus, &data_tx);
    if (ret < 0) {
      LOG_ERR("SPI write data buffer failed: %d", ret);
    }
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
// Matches reference driver EPD_HW_Init() sequence exactly
static int ssd1683_init_common(const struct ssd1683_config *cfg) {
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

  // === 4.2" EPD Init Sequence (matches EPD_4IN2_V2_Init) ===

  // Hardware reset (100ms delay)
  gpio_pin_set_dt(&cfg->rst, 1);
  k_msleep(100);
  gpio_pin_set_dt(&cfg->rst, 0);
  k_msleep(2);
  gpio_pin_set_dt(&cfg->rst, 1);
  k_msleep(100);

  wait_busy(cfg);

  // Soft reset
  ssd1683_write_cmd(cfg, 0x12); // SWRESET
  wait_busy(cfg);

  // Display update control (0x21) - 4.2" specific
  ssd1683_write_cmd(cfg, 0x21);
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);

  // Border waveform control (0x3C)
  ssd1683_write_cmd(cfg, 0x3C);
  ssd1683_write_data(cfg, 0x05);

  // Data entry mode (0x11) - X and Y increment (0x03 for 4.2")
  ssd1683_write_cmd(cfg, 0x11);
  ssd1683_write_data(cfg, 0x03);

  // Set window to full display using SetWindows function
  ssd1683_set_window(cfg, 0, 0, cfg->width - 1, cfg->height - 1);

  // Set cursor to origin
  ssd1683_set_cursor(cfg, 0, 0);

  wait_busy(cfg);

  LOG_INF("SSD1683 4.2\" init sequence completed");
  return 0;
}

// Standard initialization (full quality)
// Init common now handles everything from reference driver
int ssd1683_init(const struct ssd1683_config *cfg) {
  int ret = ssd1683_init_common(cfg);
  if (ret < 0) {
    return ret;
  }

  LOG_INF("SSD1683 initialized (standard mode)");
  return 0;
}

// Fast initialization (4.2" specific - adds temperature control)
// Matches EPD_4IN2_V2_Init_Fast()
int ssd1683_init_fast(const struct ssd1683_config *cfg) {
  // Same basic init as standard
  int ret = ssd1683_init_common(cfg);
  if (ret < 0) {
    return ret;
  }

  // Temperature sensor setup for fast mode (1s refresh)
  ssd1683_write_cmd(cfg, 0x1A);  // Write to temperature register
  ssd1683_write_data(cfg, 0x5A); // 1s mode

  // Load temperature value
  ssd1683_write_cmd(cfg, 0x22);
  ssd1683_write_data(cfg, 0x91);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);

  LOG_INF("SSD1683 4.2\" initialized (fast mode with temp control)");
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
// Matches reference driver EPD_Update()
void ssd1683_refresh(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22); // Display Update Control
  ssd1683_write_data(cfg, 0xF7);
  ssd1683_write_cmd(cfg, 0x20); // Activate Display Update Sequence
  wait_busy(cfg);
  LOG_DBG("Display refreshed (standard/full)");
}

// Partial refresh (for partial screen updates)
// Matches reference driver EPD_Part_Update()
void ssd1683_refresh_partial(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22); // Display Update Control
  ssd1683_write_data(cfg, 0xFF);
  ssd1683_write_cmd(cfg, 0x20); // Activate Display Update Sequence
  wait_busy(cfg);
  LOG_DBG("Display refreshed (partial)");
}

// Fast refresh (4.2" specific - uses 0xC7)
// Matches EPD_4IN2_V2_TurnOnDisplay_Fast()
void ssd1683_refresh_fast(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22);  // Display Update Control
  ssd1683_write_data(cfg, 0xC7); // Fast mode
  ssd1683_write_cmd(cfg, 0x20);  // Activate Display Update Sequence
  wait_busy(cfg);
  LOG_DBG("Display refreshed (fast/0xC7)");
}

// ============================================================================
// Buffer Operations (Monochrome Mode)
// ============================================================================
// NOTE: This driver operates in MONOCHROME mode only.
// All buffer operations write to BW RAM only. RED RAM is cleared during
// initialization/clear to ensure no red pixels interfere.

// Set base map for partial refresh (CRITICAL!)
// Matches reference driver EPD_SetRAMValue_BaseMap()
// Writes image to BOTH 0x24 (BW) and 0x26 (RED/base) RAM
// The 0x26 RAM acts as "previous frame" for partial update comparison
void ssd1683_set_base_map(const struct ssd1683_config *cfg,
                          uint8_t *image_buffer) {
  int width_bytes = ssd1683_calc_width_bytes(cfg->width);
  int total_bytes = width_bytes * cfg->height;

  // Write to 0x24 (Black/White RAM)
  ssd1683_write_cmd(cfg, 0x24);
  for (int i = 0; i < total_bytes; i++) {
    ssd1683_write_data(cfg, image_buffer[i]);
  }

  // Write SAME image to 0x26 (RED RAM - used as base for partial updates)
  ssd1683_write_cmd(cfg, 0x26);
  for (int i = 0; i < total_bytes; i++) {
    ssd1683_write_data(cfg, image_buffer[i]);
  }

  // Full refresh to establish the base
  ssd1683_refresh(cfg);

  LOG_INF("Base map established in both 0x24 and 0x26 RAM");
}

// Flush framebuffer to display with base map setup
void ssd1683_flush(const struct ssd1683_config *cfg, uint8_t *image_buffer) {
  // Set window to full screen
  ssd1683_set_window(cfg, 0, 0, cfg->width - 1, cfg->height - 1);
  ssd1683_set_cursor(cfg, 0, 0);

  // Use base map function to write to both RAMs for proper partial refresh
  // support
  ssd1683_set_base_map(cfg, image_buffer);

  LOG_DBG("Flushed framebuffer to display with base map");
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
// Matches 4.2" reference driver EPD_4IN2_V2_PartialDisplay()
// x, y: starting position (in pixels)
// w: width in pixels
// l: height (length) in pixels
void ssd1683_partial_display(const struct ssd1683_config *cfg, uint16_t x,
                             uint16_t y, uint16_t w, uint16_t l,
                             uint8_t *image) {
  int width_bytes = (w + 7) / 8;
  int height = l;

  // Border waveform setup for partial update (4.2" specific)
  ssd1683_write_cmd(cfg, 0x3C); // BorderWaveform
  ssd1683_write_data(cfg, 0x80);

  // Display update control for partial
  ssd1683_write_cmd(cfg, 0x21);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);

  // Border waveform again (4.2" specific)
  ssd1683_write_cmd(cfg, 0x3C);
  ssd1683_write_data(cfg, 0x80);

  // Set window and cursor to target region
  ssd1683_set_window(cfg, x, y, x + w - 1, y + l - 1);
  ssd1683_set_cursor(cfg, x, y);

  // Write to BW RAM (0x24 only, 0x26 has the base image)
  ssd1683_write_cmd(cfg, 0x24);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, image[i + j * width_bytes]);
    }
  }

  // Partial refresh
  ssd1683_refresh_partial(cfg);

  LOG_DBG("Partial display: x=%d, y=%d, w=%d, h=%d", x, y, w, l);
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
// Made public for use by display wrapper
void ssd1683_set_window(const struct ssd1683_config *cfg, uint16_t x_start,
                        uint16_t y_start, uint16_t x_end, uint16_t y_end) {
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
// Made public for use by display wrapper
void ssd1683_set_cursor(const struct ssd1683_config *cfg, uint16_t x,
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
