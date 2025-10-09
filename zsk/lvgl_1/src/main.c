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
  paint.buffer_size = 400 * 300 / 8; // 15000 bytes
  paint.x_end = 400;
  paint.y_end = 300;
  paint_Init(&paint);

  // Clear screen (white)
  paint_Fill(BLACK);

  // Draw all printable ASCII characters using Font12 in a grid

  // Assume Font12 and paint_DrawString are available
  // Print 95 printable ASCII characters (from ' ' to '~'), 16 per row

  int chars_per_row = 16;
  int start_x = 10;
  int start_y = 20;
  int spacing_x = Font12.Width + 2;
  int spacing_y = Font12.Height + 4;
  char line_buf[chars_per_row + 1];
  int ascii = 32; // ' '
  int row = 0;

  while (ascii <= 126) { // '~'
    int col;
    int n = 0;
    for (col = 0; col < chars_per_row && ascii <= 126; col++, ascii++) {
      line_buf[n++] = (char)ascii;
    }
    line_buf[n] = '\0';
    paint_DrawString(line_buf, &Font12, WHITE, start_x,
                     start_y + row * spacing_y);
    row++;
  }

  // Draw all printable ASCII characters using Font16 in a grid

  // Draw all printable ASCII characters using Font8 in a grid, nicely spaced

  chars_per_row = 8; // Fewer per row, since Font24 is large
  start_x = 10;
  start_y = 20 + row * spacing_y + 30; // Continue below previous grid
  spacing_x = Font24.Width + 4;
  spacing_y = Font24.Height + 8;
  ascii = 32; // ' '
  row = 0;

  while (ascii <= 126) { // '~'
    int col;
    int n = 0;
    for (col = 0; col < chars_per_row && ascii <= 126; col++, ascii++) {
      line_buf[n++] = (char)ascii;
    }
    line_buf[n] = '\0';
    paint_DrawString(line_buf, &Font24, WHITE, start_x,
                     start_y + row * spacing_y);
    row++;
  }

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
