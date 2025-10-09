#include "ssd1683.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ssd1683, LOG_LEVEL_INF);

// Remove hardcoded dimensions - use cfg->width and cfg->height instead

// LUT (Look-Up Table) for 4-gray mode - from reference EPD_4in2_V2.c
const unsigned char LUT_ALL[233] = {
    0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01, 0x01, 0x05, 0x0A, 0x01, 0x0A, 0x01,
    0x01, 0x01, 0x05, 0x08, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x02, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01,
    0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01, 0x05, 0x48, 0x03, 0x82,
    0x84, 0x01, 0x01, 0x01, 0x84, 0x84, 0x82, 0x00, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x0A, 0x1B, 0x8F, 0x03, 0x01, 0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01,
    0x01, 0x01, 0x05, 0x48, 0x83, 0x82, 0x04, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x02, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x8A, 0x1B, 0x8F, 0x03, 0x01,
    0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01, 0x05, 0x48, 0x83, 0x02,
    0x04, 0x01, 0x01, 0x01, 0x04, 0x04, 0x02, 0x00, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x8A, 0x9B, 0x8F, 0x03, 0x01, 0x01, 0x05, 0x4A, 0x01, 0x8A, 0x01,
    0x01, 0x01, 0x05, 0x48, 0x03, 0x42, 0x04, 0x01, 0x01, 0x01, 0x04, 0x04,
    0x42, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x07,
    0x17, 0x41, 0xA8, 0x32, 0x30,
};

// Forward declarations
static void ssd1683_set_window(const struct ssd1683_config *cfg,
                               uint16_t x_start, uint16_t y_start,
                               uint16_t x_end, uint16_t y_end);
static void ssd1683_set_cursor(const struct ssd1683_config *cfg, uint16_t x,
                               uint16_t y);
static void ssd1683_4gray_lut(const struct ssd1683_config *cfg);
static int ssd1683_init_common(const struct ssd1683_config *cfg);

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

// Common initialization code for all init functions
static int ssd1683_init_common(const struct ssd1683_config *cfg) {
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

  // Set window to full display
  ssd1683_set_window(cfg, 0, 0, cfg->width - 1, cfg->height - 1);
  // Set cursor to (0,0)
  ssd1683_set_cursor(cfg, 0, 0);
  wait_busy(cfg);

  return 0;
}

// Initialize display (basic)
int ssd1683_init(const struct ssd1683_config *cfg) {
  int ret = ssd1683_init_common(cfg);
  if (ret < 0)
    return ret;

  ssd1683_write_cmd(cfg, 0x21); // Display update control
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, 0x3C); // Border waveform
  ssd1683_write_data(cfg, 0x05);

  ssd1683_write_cmd(cfg, 0x11); // Data entry mode
  ssd1683_write_data(cfg, 0x03);

  LOG_INF("SSD1683 init done");
  return 0;
}

int ssd1683_init_fast(const struct ssd1683_config *cfg) {
  int ret = ssd1683_init_common(cfg);
  if (ret < 0)
    return ret;

  ssd1683_write_cmd(cfg, 0x21); // Display update control
  ssd1683_write_data(cfg, 0x40);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, 0x3C); // Border waveform
  ssd1683_write_data(cfg, 0x05);

  ssd1683_write_cmd(cfg, 0x1A); /* Write to temperature register */
  ssd1683_write_data(cfg, 0x5A);

  ssd1683_write_cmd(cfg, 0x22); /* Load temperature value */
  ssd1683_write_data(cfg, 0x91);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);

  ssd1683_write_cmd(cfg, 0x11); // Data entry mode
  ssd1683_write_data(cfg, 0x03);

  LOG_INF("SSD1683 init Fast done");
  return 0;
}

int ssd1683_init_gray(const struct ssd1683_config *cfg) {
  int ret = ssd1683_init_common(cfg);
  if (ret < 0)
    return ret;

  ssd1683_write_cmd(cfg, 0x21); // Display update control
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, 0x3C); // Border waveform
  ssd1683_write_data(cfg, 0x03);

  ssd1683_write_cmd(cfg, 0x0C); // Display update control
  ssd1683_write_data(cfg, 0x8B);
  ssd1683_write_data(cfg, 0x9C);
  ssd1683_write_data(cfg, 0xA4);
  ssd1683_write_data(cfg, 0x0F);

  ssd1683_4gray_lut(cfg); /* LUT */

  ssd1683_write_cmd(cfg, 0x11); // Data entry mode
  ssd1683_write_data(cfg, 0x03);

  LOG_INF("SSD1683 init 4Gray done");
  return 0;
}

// 4Gray LUT download function - from reference EPD_4in2_V2.c
static void ssd1683_4gray_lut(const struct ssd1683_config *cfg) {
  unsigned char i;

  /* WS byte 0~152, the content of VS[nX-LUTm], TP[nX], RP[n], SR[nXY], FR[n]
   * and XON[nXY] */
  ssd1683_write_cmd(cfg, 0x32);
  for (i = 0; i < 227; i++) {
    ssd1683_write_data(cfg, LUT_ALL[i]);
  }

  /* WS byte 153, the content of Option for LUT end */
  ssd1683_write_cmd(cfg, 0x3F);
  ssd1683_write_data(cfg, LUT_ALL[i++]);

  /* WS byte 154, the content of gate leveL */
  ssd1683_write_cmd(cfg, 0x03);
  ssd1683_write_data(cfg, LUT_ALL[i++]); /* VGH */

  /* WS byte 155~157, the content of source level */
  ssd1683_write_cmd(cfg, 0x04);
  ssd1683_write_data(cfg, LUT_ALL[i++]); /* VSH1 */
  ssd1683_write_data(cfg, LUT_ALL[i++]); /* VSH2 */
  ssd1683_write_data(cfg, LUT_ALL[i++]); /* VSL */

  /* WS byte 158, the content of VCOM level */
  ssd1683_write_cmd(cfg, 0x2c);
  ssd1683_write_data(cfg, LUT_ALL[i++]); /* VCOM */
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

  // ssd1683_refresh(cfg);
}

// Refresh display
void ssd1683_refresh(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22);
  ssd1683_write_data(cfg, 0xF7);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);
}

void ssd1683_refresh_fast(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22);
  ssd1683_write_data(cfg, 0xC7);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);
}

void ssd1683_refresh_partial(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22);
  ssd1683_write_data(cfg, 0xFF);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);
}

void ssd1683_refresh_gray(const struct ssd1683_config *cfg) {
  ssd1683_write_cmd(cfg, 0x22);
  ssd1683_write_data(cfg, 0xCF);
  ssd1683_write_cmd(cfg, 0x20);
  wait_busy(cfg);
}

void ssd1683_flush(const struct ssd1683_config *cfg, uint8_t *image_buffer) {
  int x = 0;
  int y = 0;
  int w = cfg->width;
  int l = cfg->height;
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
  int w = cfg->width;
  int l = cfg->height;
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
  // ssd1683_refresh(cfg);
}

// Display_Fast function - from reference EPD_4IN2_V2_Display_Fast
void ssd1683_display_fast(const struct ssd1683_config *cfg, uint8_t *image) {
  int width_bytes =
      (cfg->width % 8 == 0) ? (cfg->width / 8) : (cfg->width / 8 + 1);
  int height = cfg->height;

  ssd1683_set_window(cfg, 0, 0, cfg->width - 1, cfg->height - 1);
  ssd1683_write_cmd(cfg, 0x24);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, image[i + j * width_bytes]);
    }
  }

  ssd1683_write_cmd(cfg, 0x26);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, image[i + j * width_bytes]);
    }
  }
  ssd1683_refresh_fast(cfg);
}

// Display_4Gray function - from reference EPD_4IN2_V2_Display_4Gray
void ssd1683_display_4gray(const struct ssd1683_config *cfg, uint8_t *image) {
  int i, j, k, m;
  uint8_t temp1, temp2, temp3;

  /****Color display description****
   *	white  gray2  gray1  black
   *	0x10|  01     01     00     00
   *	0x13|  01     00     01     00
   ********************************/
  ssd1683_write_cmd(cfg, 0x24);
  /* EPD_4IN2_HEIGHT */
  /* EPD_4IN2_WIDTH */
  for (m = 0; m < cfg->height; m++) {
    for (i = 0; i < cfg->width / 8; i++) {
      temp3 = 0;
      for (j = 0; j < 2; j++) {

        temp1 = image[(m * (cfg->width / 8) + i) * 2 + j];
        for (k = 0; k < 2; k++) {
          temp2 = temp1 & 0xC0;
          if (temp2 == 0xC0) {
            temp3 |= 0x01; /* white */
          } else if (temp2 == 0x00) {
            temp3 |= 0x00; /* black */
          } else if (temp2 == 0x80) {
            temp3 |= 0x00; /* gray1 */
          } else {         /* 0x40 */
            temp3 |= 0x01; /* gray2 */
          }
          temp3 <<= 1;

          temp1 <<= 2;
          temp2 = temp1 & 0xC0;
          if (temp2 == 0xC0) { /* white */
            temp3 |= 0x01;
          } else if (temp2 == 0x00) { /* black */
            temp3 |= 0x00;
          } else if (temp2 == 0x80) {
            temp3 |= 0x00; /* gray1 */
          } else {         /* 0x40 */
            temp3 |= 0x01; /* gray2 */
          }
          if (j != 1 || k != 1) {
            temp3 <<= 1;
          }

          temp1 <<= 2;
        }
      }
      ssd1683_write_data(cfg, temp3);
    }
  }
  /* new  data */
  ssd1683_write_cmd(cfg, 0x26);
  for (m = 0; m < cfg->height; m++) {
    for (i = 0; i < cfg->width / 8; i++) {
      temp3 = 0;
      for (j = 0; j < 2; j++) {
        temp1 = image[(m * (cfg->width / 8) + i) * 2 + j];
        for (k = 0; k < 2; k++) {
          temp2 = temp1 & 0xC0;
          if (temp2 == 0xC0) {
            temp3 |= 0x01; /* white */
          } else if (temp2 == 0x00) {
            temp3 |= 0x00; /* black */
          } else if (temp2 == 0x80) {
            temp3 |= 0x01; /* gray1 */
          } else {         /* 0x40 */
            temp3 |= 0x00; /* gray2 */
          }
          temp3 <<= 1;

          temp1 <<= 2;
          temp2 = temp1 & 0xC0;
          if (temp2 == 0xC0) { /* white */
            temp3 |= 0x01;
          } else if (temp2 == 0x00) { /* black */
            temp3 |= 0x00;
          } else if (temp2 == 0x80) {
            temp3 |= 0x01; /* gray1 */
          } else {         /* 0x40 */
            temp3 |= 0x00; /* gray2 */
          }
          if (j != 1 || k != 1) {
            temp3 <<= 1;
          }

          temp1 <<= 2;
        }
      }
      ssd1683_write_data(cfg, temp3);
    }
  }
  ssd1683_refresh_gray(cfg);
}

// TurnOnDisplay functions are identical to refresh functions - use those
// instead

// PartialDisplay function - from reference EPD_4IN2_V2_PartialDisplay
void ssd1683_partial_display(const struct ssd1683_config *cfg, uint16_t x,
                             uint16_t y, uint16_t w, uint16_t l,
                             uint8_t *image) {
  int width_bytes = (w % 8 == 0) ? (w / 8) : (w / 8 + 1);
  int height = l;

  ssd1683_write_cmd(cfg, 0x3C); /* BorderWavefrom, */
  ssd1683_write_data(cfg, 0x80);

  ssd1683_write_cmd(cfg, 0x21);
  ssd1683_write_data(cfg, 0x00);
  ssd1683_write_data(cfg, 0x00);

  ssd1683_write_cmd(cfg, 0x3C);
  ssd1683_write_data(cfg, 0x80);

  ssd1683_set_window(cfg, x, y, x + w - 1, y + l - 1);
  ssd1683_set_cursor(cfg, x, y);

  ssd1683_write_cmd(cfg, 0x24);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, image[i + j * width_bytes]);
    }
  }

  ssd1683_refresh_partial(cfg);
}

// WriteDisplay function - from reference EPD_4IN2_V2_WriteDisplay
void ssd1683_write_display(const struct ssd1683_config *cfg, uint16_t x,
                           uint16_t y, uint16_t w, uint16_t l, uint8_t *image) {
  int width_bytes = (w % 8 == 0) ? (w / 8) : (w / 8 + 1);
  int height = l;

  ssd1683_set_window(cfg, x, y, x + w - 1, y + l - 1);
  ssd1683_set_cursor(cfg, x, y);
  ssd1683_write_cmd(cfg, 0x24);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, image[i + j * width_bytes]);
    }
  }

  ssd1683_write_cmd(cfg, 0x26);
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width_bytes; i++) {
      ssd1683_write_data(cfg, image[i + j * width_bytes]);
    }
  }
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
