#include "ssd1683.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ssd1683, LOG_LEVEL_INF);

#define SSD1683_WIDTH 400
#define SSD1683_HEIGHT 300

// Forward declarations
static void ssd1683_set_window(const struct ssd1683_config *cfg,
                               uint16_t x_start, uint16_t y_start,
                               uint16_t x_end, uint16_t y_end);
static void ssd1683_set_cursor(const struct ssd1683_config *cfg, uint16_t x,
                               uint16_t y);

// Simple helpers
static void wait_busy(const struct ssd1683_config *cfg) {
  LOG_INF("Waiting for BUSY pin...");
  while (gpio_pin_get_dt(&cfg->busy)) {
    k_msleep(10);
  }
}

void ssd1683_deep_sleep(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x10);
  ssd1683_write_data(cfg, 0x01);
};

// Low-level SPI commands
void ssd1683_write_cmd(const struct ssd1683_config *cfg, uint8_t cmd) {
  gpio_pin_set_dt(&cfg->dc, 0); // command mode
  struct spi_buf buf = {.buf = &cmd, .len = 1};
  struct spi_buf_set tx = {.buffers = &buf, .count = 1};
  spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
}

void ssd1683_write_data(const struct ssd1683_config *cfg, uint8_t data) {
  gpio_pin_set_dt(&cfg->dc, 1); // data mode
  struct spi_buf buf = {.buf = &data, .len = 1};
  struct spi_buf_set tx = {.buffers = &buf, .count = 1};
  spi_write(cfg->spi_dev, &cfg->spi_cfg, &tx);
}

// Hardware reset
void ssd1683_reset(const struct ssd1683_config *cfg) {
  gpio_pin_set_dt(&cfg->rst, 0);
  k_msleep(10);
  gpio_pin_set_dt(&cfg->rst, 1);
  k_msleep(10);
  LOG_INF("HW Reset done");
}

// Initialize display (basic)
int ssd1683_init(const struct ssd1683_config *cfg) {
  if (!device_is_ready(cfg->spi_dev) || !device_is_ready(cfg->dc.port) ||
      !device_is_ready(cfg->rst.port) || !device_is_ready(cfg->busy.port)) {
    LOG_ERR("One or more devices not ready");
    return -ENODEV;
  }

  gpio_pin_configure_dt(&cfg->dc, GPIO_OUTPUT_ACTIVE);
  gpio_pin_configure_dt(&cfg->rst, GPIO_OUTPUT_ACTIVE);
  gpio_pin_configure_dt(&cfg->busy, GPIO_INPUT);

  ssd1683_reset(cfg);

  wait_busy(cfg);
  ssd1683_write_cmd(cfg, 0x12); // soft reset
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x21); // Display update control
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, 0x3C); // Border waveform
  ssd1683_write_data(cfg, 0x05);

  // Load temperature value (SSD1683 sequence)
  ssd1683_write_cmd(cfg, 0x22); // Load temperature value
  ssd1683_write_data(cfg, 0x91);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x11); // Data entry mode
  ssd1683_write_data(cfg, 0x03);

  // Set window to full display
  ssd1683_set_window(cfg, 0, 0, SSD1683_WIDTH - 1, SSD1683_HEIGHT - 1);

  // Set cursor to (0,0)
  ssd1683_set_cursor(cfg, 0, 0);

  wait_busy(cfg);

  LOG_INF("SSD1683 init done");
  return 0;
}

// Clear display (BW=white, RED=off)
void ssd1683_clear(const struct ssd1683_config *cfg) {
  int bytes_per_line = cfg->width / 8;
  int total_bytes = bytes_per_line * cfg->height;

  ssd1683_write_cmd(cfg, 0x24); // BW RAM
  for (int i = 0; i < total_bytes; i++)
    ssd1683_write_data(cfg, 0xFF);

  ssd1683_write_cmd(cfg, 0x26); // RED RAM
  for (int i = 0; i < total_bytes; i++)
    ssd1683_write_data(cfg, 0xFF);

  ssd1683_refresh(cfg);
}

// Refresh display
void ssd1683_refresh(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22);
  ssd1683_write_data(cfg, 0xF7);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);
}

void ssd1683_flush(const struct ssd1683_config *cfg, uint8_t *image_buffer) {
  int x = 0;
  int y = 0;
  int w = SSD1683_WIDTH;
  int l = SSD1683_HEIGHT;
  int width_bytes = (w % 8 == 0) ? (w / 8) : (w / 8 + 1);
  int height = l;

  // Set window to full screen
  ssd1683_set_window(cfg, x, y, x + w - 1, y + l - 1);
  ssd1683_set_cursor(cfg, x, y);

  // Write BW buffer (0x24 command)
  ssd1683_write_cmd(cfg, 0x24);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, image_buffer[i + j * width_bytes]);
    }
  }

  // Write RED buffer (0x26 command) - same buffer
  ssd1683_write_cmd(cfg, 0x26);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, image_buffer[i + j * width_bytes]);
    }
  }
  ssd1683_refresh(cfg);
}

void ssd1683_flush_from_paint(const struct ssd1683_config *cfg,
                              uint8_t *wb_buffer, uint8_t *rw_buffer) {
  int x = 0;
  int y = 0;
  int w = SSD1683_WIDTH;
  int l = SSD1683_HEIGHT;
  int width_bytes = (w % 8 == 0) ? (w / 8) : (w / 8 + 1);
  int height = l;

  // Set window to full screen
  ssd1683_set_window(cfg, x, y, x + w - 1, y + l - 1);
  ssd1683_set_cursor(cfg, x, y);

  // Write BW buffer (0x24 command)
  ssd1683_write_cmd(cfg, 0x24);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, wb_buffer[i + j * width_bytes]);
    }
  }

  // Write RED buffer (0x26 command)
  ssd1683_write_cmd(cfg, 0x26);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, rw_buffer[i + j * width_bytes]);
    }
  }
  ssd1683_refresh(cfg);
}

// Set window for SSD1683 (port of EPD_4IN2_V2_SetWindows)
static void ssd1683_set_window(const struct ssd1683_config *cfg,
                               uint16_t x_start, uint16_t y_start,
                               uint16_t x_end, uint16_t y_end) {
  // Set RAM X address start/end
  ssd1683_write_cmd(cfg, 0x44);
  ssd1683_write_data(cfg, (x_start >> 3) & 0xFF);
  ssd1683_write_data(cfg, (x_end >> 3) & 0xFF);

  // Set RAM Y address start/end
  ssd1683_write_cmd(cfg, 0x45);
  ssd1683_write_data(cfg, y_start & 0xFF);
  ssd1683_write_data(cfg, (y_start >> 8) & 0xFF);
  ssd1683_write_data(cfg, y_end & 0xFF);
  ssd1683_write_data(cfg, (y_end >> 8) & 0xFF);
}

// Set cursor for SSD1683 (port of EPD_4IN2_V2_SetCursor)
static void ssd1683_set_cursor(const struct ssd1683_config *cfg, uint16_t x,
                               uint16_t y) {
  // Set RAM X address counter
  ssd1683_write_cmd(cfg, 0x4E);
  ssd1683_write_data(cfg, x & 0xFF);

  // Set RAM Y address counter
  ssd1683_write_cmd(cfg, 0x4F);
  ssd1683_write_data(cfg, y & 0xFF);
  ssd1683_write_data(cfg, (y >> 8) & 0xFF);
}
