#include "ssd1683.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ssd1683, LOG_LEVEL_INF);

// Internal state management
static ssd1683_refresh_mode_t current_refresh_mode = SSD1683_REFRESH_FULL;
static bool driver_initialized = false;

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

void ssd1683_sleep(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x10); // DEEP_SLEEP
  ssd1683_write_data(cfg, 0x01);
  k_msleep(200);
}

// ============================================================================
// Utility Functions
// ============================================================================

void ssd1683_set_refresh_mode(const struct ssd1683_config *cfg,
                              ssd1683_refresh_mode_t mode) {
  current_refresh_mode = mode;
  LOG_DBG("Refresh mode set to: %d", mode);
}

ssd1683_refresh_mode_t
ssd1683_get_refresh_mode(const struct ssd1683_config *cfg) {
  return current_refresh_mode;
}

bool ssd1683_is_busy(const struct ssd1683_config *cfg) {
  return gpio_pin_get_dt(&cfg->busy);
}

// ============================================================================
// Window and Cursor Management
// ============================================================================

static void ssd1683_set_windows(const struct ssd1683_config *cfg,
                                uint16_t xstart, uint16_t ystart, uint16_t xend,
                                uint16_t yend) {
  ssd1683_write_cmd(cfg, 0x44); // SET_RAM_X_ADDRESS_START_END_POSITION
  ssd1683_write_data(cfg, (xstart >> 3) & 0xFF);
  ssd1683_write_data(cfg, (xend >> 3) & 0xFF);

  ssd1683_write_cmd(cfg, 0x45); // SET_RAM_Y_ADDRESS_START_END_POSITION
  ssd1683_write_data(cfg, ystart & 0xFF);
  ssd1683_write_data(cfg, (ystart >> 8) & 0xFF);
  ssd1683_write_data(cfg, yend & 0xFF);
  ssd1683_write_data(cfg, (yend >> 8) & 0xFF);
}

static void ssd1683_set_cursor(const struct ssd1683_config *cfg, uint16_t x,
                               uint16_t y) {
  ssd1683_write_cmd(cfg, 0x4E); // SET_RAM_X_ADDRESS_COUNTER
  ssd1683_write_data(cfg, x & 0xFF);

  ssd1683_write_cmd(cfg, 0x4F); // SET_RAM_Y_ADDRESS_COUNTER
  ssd1683_write_data(cfg, y & 0xFF);
  ssd1683_write_data(cfg, (y >> 8) & 0xFF);
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

void ssd1683_init(const struct ssd1683_config *cfg) {
  ssd1683_pin_init(cfg);
  ssd1683_reset(cfg);
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x12); // soft reset
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x21); // Display update control
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, 0x3C); // BorderWavefrom
  ssd1683_write_data(cfg, 0x05);

  ssd1683_write_cmd(cfg, 0x11);  // data entry mode
  ssd1683_write_data(cfg, 0x03); // X-mode

  ssd1683_set_windows(cfg, 0, 0, cfg->width - 1, cfg->height - 1);
  ssd1683_set_cursor(cfg, 0, 0);

  wait_busy(cfg);
  driver_initialized = true;
  current_refresh_mode = SSD1683_REFRESH_FULL;
  LOG_INF("Initialization completed");
}

void ssd1683_init_fast(const struct ssd1683_config *cfg) {
  ssd1683_pin_init(cfg);
  ssd1683_reset(cfg);
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x12); // soft reset
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x21);
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, 0x3C);
  ssd1683_write_data(cfg, 0x05);

  // 1s refresh time
  ssd1683_write_cmd(cfg, 0x1A); // Write to temperature register
  ssd1683_write_data(cfg, 0x5A);

  ssd1683_write_cmd(cfg, 0x22); // Load temperature value
  ssd1683_write_data(cfg, 0x91);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x11);  // data entry mode
  ssd1683_write_data(cfg, 0x03); // X-mode

  ssd1683_set_windows(cfg, 0, 0, cfg->width - 1, cfg->height - 1);
  ssd1683_set_cursor(cfg, 0, 0);

  wait_busy(cfg);
  driver_initialized = true;
  current_refresh_mode = SSD1683_REFRESH_FAST;
  LOG_INF("Fast initialization completed");
}

void ssd1683_init_4gray(const struct ssd1683_config *cfg) {
  ssd1683_pin_init(cfg);
  ssd1683_reset(cfg);
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x12); // SWRESET
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x21);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, 0x3C);
  ssd1683_write_data(cfg, 0x03);

  ssd1683_write_cmd(cfg, 0x0C);  // BTST
  ssd1683_write_data(cfg, 0x8B); // 8B
  ssd1683_write_data(cfg, 0x9C); // 9C
  ssd1683_write_data(cfg, 0xA4); // A4
  ssd1683_write_data(cfg, 0x0F); // 0F

  // TODO: Add LUT loading for 4-level grayscale
  // ssd1683_4gray_lut(cfg);

  ssd1683_write_cmd(cfg, 0x11);  // data entry mode
  ssd1683_write_data(cfg, 0x03); // X-mode

  ssd1683_set_windows(cfg, 0, 0, cfg->width - 1, cfg->height - 1);
  ssd1683_set_cursor(cfg, 0, 0);

  driver_initialized = true;
  current_refresh_mode = SSD1683_REFRESH_FULL;
  LOG_INF("4-level grayscale initialization completed");
}

// ============================================================================
// Display Functions
// ============================================================================

void ssd1683_clear(const struct ssd1683_config *cfg) {
  uint16_t width =
      (cfg->width % 8 == 0) ? (cfg->width / 8) : (cfg->width / 8 + 1);
  uint16_t height = cfg->height;

  ssd1683_write_cmd(cfg, 0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      ssd1683_write_data(cfg, 0xFF);
    }
  }

  ssd1683_write_cmd(cfg, 0x26);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      ssd1683_write_data(cfg, 0xFF);
    }
  }
  ssd1683_turn_on_display(cfg);
}

void ssd1683_display(const struct ssd1683_config *cfg, uint8_t *image) {
  uint16_t width =
      (cfg->width % 8 == 0) ? (cfg->width / 8) : (cfg->width / 8 + 1);
  uint16_t height = cfg->height;

  ssd1683_set_windows(cfg, 0, 0, cfg->width - 1, cfg->height - 1);
  ssd1683_write_cmd(cfg, 0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      ssd1683_write_data(cfg, image[i + j * width]);
    }
  }

  ssd1683_write_cmd(cfg, 0x26);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      ssd1683_write_data(cfg, image[i + j * width]);
    }
  }
  ssd1683_turn_on_display(cfg);
}

void ssd1683_display_fast(const struct ssd1683_config *cfg, uint8_t *image) {
  uint16_t width =
      (cfg->width % 8 == 0) ? (cfg->width / 8) : (cfg->width / 8 + 1);
  uint16_t height = cfg->height;

  ssd1683_set_windows(cfg, 0, 0, cfg->width - 1, cfg->height - 1);
  ssd1683_write_cmd(cfg, 0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      ssd1683_write_data(cfg, image[i + j * width]);
    }
  }

  ssd1683_write_cmd(cfg, 0x26);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      ssd1683_write_data(cfg, image[i + j * width]);
    }
  }
  ssd1683_turn_on_display_fast(cfg);
}

void ssd1683_display_4gray(const struct ssd1683_config *cfg, uint8_t *image) {
  // TODO: Implement 4-level grayscale display
  LOG_WRN("4-level grayscale display not yet implemented");
}

void ssd1683_partial_display(const struct ssd1683_config *cfg, uint16_t x,
                             uint16_t y, uint16_t w, uint16_t h,
                             uint8_t *image) {
  uint16_t width = (w % 8 == 0) ? (w / 8) : (w / 8 + 1);
  uint16_t height = h;

  ssd1683_write_cmd(cfg, 0x3C); // BorderWavefrom
  ssd1683_write_data(cfg, 0x80);

  ssd1683_write_cmd(cfg, 0x21);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, 0x3C);
  ssd1683_write_data(cfg, 0x80);

  ssd1683_set_windows(cfg, x, y, x + w - 1, y + h - 1);
  ssd1683_set_cursor(cfg, x, y);

  ssd1683_write_cmd(cfg, 0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      ssd1683_write_data(cfg, image[i + j * width]);
    }
  }

  ssd1683_turn_on_display_partial(cfg);
}

void ssd1683_write_display(const struct ssd1683_config *cfg, uint16_t x,
                           uint16_t y, uint16_t w, uint16_t h, uint8_t *image) {
  uint16_t width = (w % 8 == 0) ? (w / 8) : (w / 8 + 1);
  uint16_t height = h;

  ssd1683_set_windows(cfg, x, y, x + w - 1, y + h - 1);
  ssd1683_set_cursor(cfg, x, y);
  ssd1683_write_cmd(cfg, 0x24);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      ssd1683_write_data(cfg, image[i + j * width]);
    }
  }

  ssd1683_write_cmd(cfg, 0x26);
  for (uint16_t j = 0; j < height; j++) {
    for (uint16_t i = 0; i < width; i++) {
      ssd1683_write_data(cfg, image[i + j * width]);
    }
  }
}

// ============================================================================
// Update Functions
// ============================================================================

void ssd1683_turn_on_display(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22);
  ssd1683_write_data(cfg, 0xF7);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);
}

void ssd1683_turn_on_display_fast(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22);
  ssd1683_write_data(cfg, 0xC7);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);
}

void ssd1683_turn_on_display_partial(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22);
  ssd1683_write_data(cfg, 0xFF);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);
}
