#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ssd1683.h"

LOG_MODULE_REGISTER(epd_main, LOG_LEVEL_INF);
#define LED0_NODE DT_ALIAS(led0)
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define ARDUINO_SPI DT_NODELABEL(arduino_spi)
#define EPD_SPI_DEVICE DT_NODELABEL(epd_spi_device)
#define EPD_SPI_CS_DT_SPEC                                                     \
  SPI_CS_GPIOS_DT_SPEC_GET(DT_NODELABEL(epd_spi_device))

// SSD1683 display configuration
static const struct ssd1683_config epd_cfg = {
    .spi_dev = DEVICE_DT_GET(ARDUINO_SPI),
    .spi_cfg =
        {
            .operation = SPI_WORD_SET(8) | SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
                         SPI_MODE_CPOL | SPI_MODE_CPHA,
            .frequency = 4000000,
            .slave = 0,
            .cs = {.gpio = EPD_SPI_CS_DT_SPEC, .delay = 0},
        },
    .dc = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_dc_gpios),
    .rst = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_rst_gpios),
    .busy = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, epd_busy_gpios),
    .width = 400,
    .height = 300,
};

int main(void) {
  int ret;

  // Initialize display in 4-gray mode (required for 3-color)
  ret = ssd1683_init(&epd_cfg);
  if (ret < 0) {
    return ret;
  }
  ssd1683_clear(&epd_cfg);
  ssd1683_refresh(&epd_cfg);
  // // Allocate framebuffers
  static uint8_t fb_bw[15000] = {0xFF};  // (400 x 300 / 8)
  static uint8_t fb_red[15000] = {0xFF}; // (400 x 300 / 8)
  int width = epd_cfg.width;
  int height = epd_cfg.height;

  // Clear framebuffers to white (all bits set)
  memset(fb_bw, 0xFF, sizeof(fb_bw));
  memset(fb_red, 0xFF, sizeof(fb_red));

  // Draw THICK RED vertical line (x=50 to x=59, y=40..260)
  for (int x = 50; x < 60; ++x) {
    ssd1683_draw_vline(fb_red, x, 40, 260, width, height);
  }
  // Draw THICK RED horizontal line (y=120 to y=129, x=60..340)
  for (int y = 120; y < 130; ++y) {
    ssd1683_draw_hline(fb_red, 60, 340, y, width, height);
  }

  // Draw THICK BLACK vertical line (x=340 to x=349, y=40..260)
  for (int x = 340; x < 350; ++x) {
    ssd1683_draw_vline(fb_bw, x, 40, 260, width, height);
  }
  // Draw THICK BLACK horizontal line (y=170 to y=179, x=60..340)
  for (int y = 170; y < 180; ++y) {
    ssd1683_draw_hline(fb_bw, 60, 340, y, width, height);
  }

  // // Draw THICK WHITE vertical line: by erasing red/black in a band
  // (x=195..205,
  // // y=40..260)
  // for (int x = 195; x <= 205; ++x) {
  //   for (int y = 40; y <= 260; ++y) {
  //     int width_bytes = (width + 7) / 8;
  //     int byte_index = (x / 8) + y * width_bytes;
  //     uint8_t mask = 1 << (7 - (x % 8));
  //     fb_bw[byte_index] |= mask;  // white: set bit in black buffer
  //     fb_red[byte_index] |= mask; //      : set bit in red buffer
  //   }
  // }
  // // Draw THICK WHITE horizontal line (erasing a band at y = 145..154, x
  // // = 70..330)
  // for (int y = 145; y <= 154; ++y) {
  //   for (int x = 70; x <= 330; ++x) {
  //     int width_bytes = (width + 7) / 8;
  //     int byte_index = (x / 8) + y * width_bytes;
  //     uint8_t mask = 1 << (7 - (x % 8));
  //     fb_bw[byte_index] |= mask;
  //     fb_red[byte_index] |= mask;
  //   }
  // }

  ssd1683_flush_from_paint(&epd_cfg, fb_bw, fb_red);
  ssd1683_refresh(&epd_cfg);

  return 0;
}
