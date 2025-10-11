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

  // Initialize display
  ret = ssd1683_init(&epd_cfg);
  if (ret < 0) {
    return ret;
  }
  ssd1683_clear(&epd_cfg);

  // Allocate framebuffers
  static uint8_t fb_bw[15000] = {0xFF};  // Black layer
  static uint8_t fb_red[15000] = {0xFF}; // Red layer
  int width = epd_cfg.width;
  int height = epd_cfg.height;

  // Clear framebuffers
  memset(fb_bw, 0xFF, sizeof(fb_bw));
  memset(fb_red, 0xFF, sizeof(fb_red));

  // === Draw thick box ===
  void draw_box(uint8_t * fb, int x0, int y0, int x1, int y1, int thickness) {
    for (int i = 0; i < thickness; ++i) {
      ssd1683_draw_hline(fb, x0, x1, y0 + i, width, height); // Top
      ssd1683_draw_hline(fb, x0, x1, y1 - i, width, height); // Bottom
      ssd1683_draw_vline(fb, x0 + i, y0, y1, width, height); // Left
      ssd1683_draw_vline(fb, x1 - i, y0, y1, width, height); // Right
    }
  }

  // === Center box parameters ===
  int box_width = 200;
  int box_height = 200;
  int box_x0 = (width - box_width) / 2;
  int box_y0 = (height - box_height) / 2;
  int box_x1 = box_x0 + box_width;
  int box_y1 = box_y0 + box_height;

  draw_box(fb_bw, box_x0, box_y0, box_x1, box_y1, 25); // Draw box in center

  // === Draw checkerboard inside the box ===
  int square_size = 20;
  for (int row = 0; row < box_height / square_size; ++row) {
    for (int col = 0; col < box_width / square_size; ++col) {
      if ((row + col) % 2 == 0) {
        int sx0 = box_x0 + col * square_size;
        int sy0 = box_y0 + row * square_size;
        for (int y = 0; y < square_size; ++y) {
          ssd1683_draw_hline(fb_bw, sx0, sx0 + square_size - 1, sy0 + y, width,
                             height);
        }
      }
    }
  }

  // === Display the image ===
  ssd1683_flush_from_paint(&epd_cfg, fb_bw, fb_red);
  ssd1683_refresh(&epd_cfg);
  ssd1683_deep_sleep(&epd_cfg);
  return 0;
}
