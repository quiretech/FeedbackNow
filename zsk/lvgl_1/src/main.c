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
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

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

  LOG_INF("Starting SSD1683 EPD test");

  // Configure LED
  if (!device_is_ready(led.port)) {
    LOG_ERR("LED device not ready");
  } else {
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
      LOG_ERR("Failed to configure LED: %d", ret);
    }
  }

  // Initialize display
  ret = ssd1683_init(&epd_cfg);
  if (ret < 0) {
    LOG_ERR("SSD1683 init failed: %d", ret);
  }

  // Create paint library buffers
  static uint8_t wb_buffer[15000]; // Black/White buffer
  static uint8_t rw_buffer[15000]; // Red buffer

  // Initialize paint library
  paint_obj_t paint;
  paint.width = 400;
  paint.height = 300;
  paint.direction = PAINT_DIRECTION_0;
  paint.scanmode = PAINT_SCAN_MODE_1;
  paint.wb_buffer = wb_buffer;
  paint.rw_buffer = rw_buffer;
  paint_Init(&paint);

  // Clear screen (white)
  paint_Fill(WHITE);

  // Draw three centered boxes
  paint_rect_t rect;
  int screen_width = 400;
  int box_widths[3] = {40, 60, 80};
  int box_heights[3] = {40, 60, 80};
  int box_y[3] = {30, 90, 180}; // vertical positions for each box

  for (int i = 0; i < 3; i++) {
    rect.width = box_widths[i];
    rect.height = box_heights[i];
    rect.x = (screen_width - rect.width) / 2;
    rect.y = box_y[i];
    paint_FillRect(BLACK, &rect);
  }

  // Draw centered text below the boxes
  const char *text = "QUICK BROWN FOX JUMPS OVER THE LAZY DOG";
  sFONT *font = &Font12;
  int text_len = strlen(text);
  int text_pixel_width = text_len * font->Width;
  int text_x = (screen_width - text_pixel_width) / 2;
  int text_y = box_y[2] + box_heights[2] + 20; // 20px below last box

  // Clamp text_x and text_y to be non-negative
  if (text_x < 0)
    text_x = 0;
  if (text_y + font->Height > paint.height)
    text_y = paint.height - font->Height;

  paint_DrawString(text, font, BLACK, text_x, text_y);

  // Send paint buffers to display
  ssd1683_flush_from_paint(&epd_cfg, wb_buffer, rw_buffer);

  LOG_INF("Display initialized with paint library demo");

  // Blink LED to show main loop running
  while (1) {
    if (device_is_ready(led.port)) {
      gpio_pin_toggle_dt(&led);
    }
    k_msleep(1000);
  }
  return 0;
}
