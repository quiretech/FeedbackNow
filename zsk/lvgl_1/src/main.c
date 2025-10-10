#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fonts.h"
#include "paint.h"
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

// Paint system buffers
static uint8_t wb_buffer[15000]; // White/Black buffer
static uint8_t rw_buffer[15000]; // Red/White buffer
static paint_obj_t paint_obj;

int main(void) {
  int ret;

  // Initialize display
  ret = ssd1683_init(&epd_cfg);
  if (ret < 0) {
    return ret;
  }
  ssd1683_clear(&epd_cfg);
  ssd1683_refresh(&epd_cfg);
  // Initialize paint system
  paint_obj.scanmode = PAINT_SCAN_MODE_1;
  paint_obj.direction = PAINT_DIRECTION_0;
  paint_obj.width = epd_cfg.width;
  paint_obj.height = epd_cfg.height;
  paint_obj.wb_buffer = wb_buffer;
  paint_obj.rw_buffer = rw_buffer;
  paint_obj.buffer_size = epd_cfg.height * (epd_cfg.width / 8);

  paint_Init(&paint_obj);

  // Clear screen to white
  paint_Fill(BLACK);

  // Draw some boxes
  paint_rect_t box1 = {50, 50, 80, 60};
  paint_FillRect(WHITE, &box1);

  paint_rect_t box2 = {150, 50, 80, 60};
  paint_DrawRect(WHITE, &box2);

  paint_rect_t box3 = {250, 50, 80, 60};
  paint_FillRect(WHITE, &box3);

  // Draw "Hello World" using Font16
  paint_DrawString("Hello World", &Font16, WHITE, 50, 150);

  // Display the result
  ssd1683_flush_from_paint(&epd_cfg, wb_buffer, rw_buffer);
  ssd1683_refresh_fast(&epd_cfg);
  return 0;
}
