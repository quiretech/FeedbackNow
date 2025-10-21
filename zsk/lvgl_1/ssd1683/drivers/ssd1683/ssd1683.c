#include "ssd1683.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ssd1683, LOG_LEVEL_INF);

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
static int ssd1683_pin_init(const struct ssd1683_config *cfg) {
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

  return 0;
}

void ssd1683_hw_init(const struct ssd1683_config *cfg) {
  ssd1683_pin_init(cfg);
  ssd1683_reset(cfg);

  wait_busy(cfg);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SWRESET);
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, SSD1683_CMD_DRIVER_OUTPUT);
  ssd1683_write_data(cfg, (cfg->height - 1) % 256);
  ssd1683_write_data(cfg, (cfg->height - 1) / 256);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x05);
  ssd1683_write_cmd(cfg, SSD1683_CMD_DATA_ENTRY_MODE);
  ssd1683_write_data(cfg, 0x01);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, cfg->width / 8 - 1);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y);
  ssd1683_write_data(cfg, (cfg->height - 1) % 256);
  ssd1683_write_data(cfg, (cfg->height - 1) / 256);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X_COUNT);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y_COUNT);
  ssd1683_write_data(cfg, (cfg->height - 1) % 256);
  ssd1683_write_data(cfg, (cfg->height - 1) / 256);

  wait_busy(cfg);
  LOG_INF("Standard initialization completed");
}

void ssd1683_hw_init_fast(const struct ssd1683_config *cfg) {
  ssd1683_pin_init(cfg);
  ssd1683_reset(cfg);
  wait_busy(cfg);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SWRESET);
  wait_busy(cfg);
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x05);
  ssd1683_write_cmd(cfg, SSD1683_CMD_TEMP_WRITE);
  ssd1683_write_data(cfg, 0x6E);
  ssd1683_write_cmd(cfg, SSD1683_CMD_TEMP_LOAD);
  ssd1683_write_data(cfg, 0x91);
  ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
  wait_busy(cfg);
  ssd1683_write_cmd(cfg, SSD1683_CMD_DATA_ENTRY_MODE);
  ssd1683_write_data(cfg, 0x01);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, cfg->width / 8 - 1);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y);
  ssd1683_write_data(cfg, (cfg->height - 1) % 256);
  ssd1683_write_data(cfg, (cfg->height - 1) / 256);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X_COUNT);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y_COUNT);
  ssd1683_write_data(cfg, (cfg->height - 1) % 256);
  ssd1683_write_data(cfg, (cfg->height - 1) / 256);
  wait_busy(cfg);
  LOG_INF("Fast initialization completed");
}

void ssd1683_update(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  ssd1683_write_data(cfg, 0xF7);
  ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
  wait_busy(cfg);
  LOG_INF("Display update completed");
}

void ssd1683_update_fast(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  ssd1683_write_data(cfg, 0xC7);
  ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
  wait_busy(cfg);
  LOG_INF("Fast display update completed");
}

void ssd1683_update_4g(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  ssd1683_write_data(cfg, 0xCF);
  ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
  wait_busy(cfg);
  LOG_INF("4G display update completed");
}

void ssd1683_update_partial(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  ssd1683_write_data(cfg, 0xFF);
  ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
  wait_busy(cfg);
  LOG_INF("Partial display update completed");
}

void ssd1683_write_ram_bw(const struct ssd1683_config *cfg, const uint8_t *data,
                          uint16_t length) {
  ssd1683_write_cmd(cfg,
                    SSD1683_CMD_WRITE_RAM); // Write RAM for black(0)/white (1)
  for (uint16_t i = 0; i < length; i++) {
    ssd1683_write_data(cfg, data[i]);
  }
  ssd1683_write_cmd(cfg,
                    SSD1683_CMD_WRITE_RAM2); // Write RAM for black(0)/white (1)
  for (uint16_t i = 0; i < length; i++) {
    ssd1683_write_data(cfg, data[i]);
  }
}

// ============================================================================
// Additional Initialization Functions
// ============================================================================

void ssd1683_hw_init_4g(const struct ssd1683_config *cfg) {
  ssd1683_pin_init(cfg);
  ssd1683_reset(cfg);
  wait_busy(cfg);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SWRESET);
  wait_busy(cfg);
  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x05);
  ssd1683_write_cmd(cfg, SSD1683_CMD_TEMP_WRITE);
  ssd1683_write_data(cfg, 0x5A);
  ssd1683_write_cmd(cfg, SSD1683_CMD_TEMP_LOAD);
  ssd1683_write_data(cfg, 0x91);
  ssd1683_write_cmd(cfg, SSD1683_CMD_MASTER_ACTIVATION);
  wait_busy(cfg);
  ssd1683_write_cmd(cfg, SSD1683_CMD_DATA_ENTRY_MODE);
  ssd1683_write_data(cfg, 0x01);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, cfg->width / 8 - 1);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y);
  ssd1683_write_data(cfg, (cfg->height - 1) % 256);
  ssd1683_write_data(cfg, (cfg->height - 1) / 256);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X_COUNT);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y_COUNT);
  ssd1683_write_data(cfg, (cfg->height - 1) % 256);
  ssd1683_write_data(cfg, (cfg->height - 1) / 256);
  wait_busy(cfg);
  LOG_INF("4G initialization completed");
}

void ssd1683_fillwhite(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM);
  for (uint16_t i = 0; i < cfg->width * cfg->height / 8; i++) {
    ssd1683_write_data(cfg, 0xFF);
  }
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM2);
  for (uint16_t i = 0; i < cfg->width * cfg->height / 8; i++) {
    ssd1683_write_data(cfg, 0xFF);
  }
  ssd1683_update(cfg);
}

void ssd1683_fillblack(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM);
  for (uint16_t i = 0; i < cfg->width * cfg->height / 8; i++) {
    ssd1683_write_data(cfg, 0x00);
  }
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM2);
  for (uint16_t i = 0; i < cfg->width * cfg->height / 8; i++) {
    ssd1683_write_data(cfg, 0x00);
  }
  ssd1683_update(cfg);
}

void ssd1683_SetRAMValue_BaseMap(const struct ssd1683_config *cfg,
                                 const uint8_t *data, uint16_t length) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM);
  for (uint16_t i = 0; i < length; i++) {
    ssd1683_write_data(cfg, data[i]);
  }
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM2);
  for (uint16_t i = 0; i < length; i++) {
    ssd1683_write_data(cfg, data[i]);
  }
  ssd1683_update(cfg);
}

// ============================================================================
// Partial Refresh Functions
// ============================================================================

// Partial refresh initialization - different from full refresh
void ssd1683_hw_init_partial(const struct ssd1683_config *cfg) {
  ssd1683_pin_init(cfg);
  ssd1683_reset(cfg);
  wait_busy(cfg);

  // Set border waveform for partial refresh (0x80 instead of 0x05)
  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x80);

  // Display update control for partial refresh
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);

  wait_busy(cfg);
  LOG_INF("Partial refresh initialization completed");
}

// Set base/background image for partial refresh (CRITICAL for stable display)
void ssd1683_set_base_map(const struct ssd1683_config *cfg, const uint8_t *data,
                          uint16_t length) {
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM);
  for (uint16_t i = 0; i < length; i++) {
    ssd1683_write_data(cfg, data[i]);
  }
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM2);
  for (uint16_t i = 0; i < length; i++) {
    ssd1683_write_data(cfg, data[i]);
  }
  ssd1683_update_partial(cfg);
  LOG_INF("Base map set for partial refresh");
}

// Core partial refresh function - update specific rectangular region
void ssd1683_partial_refresh(const struct ssd1683_config *cfg, uint16_t x_start,
                             uint16_t y_start, const uint8_t *data,
                             uint16_t width, uint16_t height) {
  uint16_t x_end, y_end;

  // Calculate byte-aligned coordinates
  x_start = x_start / 8;             // Convert to byte address
  x_end = x_start + (width / 8) - 1; // Calculate end byte address
  y_start = y_start;                 // Y coordinates as-is
  y_end = y_start + height - 1;      // Calculate end Y address

  // Reset for partial refresh
  ssd1683_reset(cfg);

  // Set border waveform for partial refresh
  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x80);

  // Display update control for partial refresh
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);

  // Set RAM X address window
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X);
  ssd1683_write_data(cfg, x_start);
  ssd1683_write_data(cfg, x_end);

  // Set RAM Y address window
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y);
  ssd1683_write_data(cfg, y_start % 256);
  ssd1683_write_data(cfg, y_start / 256);
  ssd1683_write_data(cfg, y_end % 256);
  ssd1683_write_data(cfg, y_end / 256);

  // Set RAM address counters
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_X_COUNT);
  ssd1683_write_data(cfg, x_start);
  ssd1683_write_cmd(cfg, SSD1683_CMD_SET_RAM_Y_COUNT);
  ssd1683_write_data(cfg, y_start % 256);
  ssd1683_write_data(cfg, y_start / 256);

  // Write image data to RAM
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM);
  for (uint16_t i = 0; i < (height * width / 8); i++) {
    ssd1683_write_data(cfg, data[i]);
  }

  // Trigger partial update
  ssd1683_update_partial(cfg);
  LOG_INF("Partial refresh completed: %dx%d at (%d,%d)", width, height,
          x_start * 8, y_start);
}

// Full screen partial refresh (faster than full refresh, no flickering)
void ssd1683_partial_refresh_full(const struct ssd1683_config *cfg,
                                  const uint8_t *data, uint16_t length) {
  // Reset for partial refresh
  ssd1683_reset(cfg);

  // Set border waveform for partial refresh
  ssd1683_write_cmd(cfg, SSD1683_CMD_BORDER_WAVEFORM);
  ssd1683_write_data(cfg, 0x80);

  // Display update control for partial refresh
  ssd1683_write_cmd(cfg, SSD1683_CMD_DISPLAY_UPDATE);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);

  // Write image data to RAM
  ssd1683_write_cmd(cfg, SSD1683_CMD_WRITE_RAM);
  for (uint16_t i = 0; i < length; i++) {
    ssd1683_write_data(cfg, data[i]);
  }

  // Trigger partial update
  ssd1683_update_partial(cfg);
  LOG_INF("Full screen partial refresh completed");
}