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
            .frequency = 7000000,
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

  LOG_INF("Starting SSD1683 EPD font demo");

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

  // Clear screen (black background)
  paint_Fill(BLACK);

  // --- Beautiful UI Layout ---

  // Outer border
  paint_rect_t border_rect = {10, 10, 380, 280};
  paint_FillRect(WHITE, &border_rect);

  // Inner background (main panel)
  paint_rect_t panel_rect = {20, 20, 360, 260};
  paint_FillRect(BLACK, &panel_rect);

  // Draw heading text (not in a box, just above the cards)
  const char *heading = "LAST CLEANED AT:";
  int heading_width = Font24.Width * (int)strlen(heading);
  int heading_x = 20 + (360 - heading_width) / 2;
  int heading_y = 30; // Place above the cards, not in a bar
  paint_DrawString(heading, &Font24, WHITE, heading_x, heading_y);

  // Timestamp "card" backgrounds
  int card_w = 320;
  int card_h = Font16.Height + 12;
  int card_x = 40;
  int first_card_y =
      heading_y + Font24.Height + 16; // Place cards below heading
  int card_spacing = 18;

  const char *timestamps[3] = {"2025-06-01 14:23", "2025-05-28 09:10",
                               "2025-05-20 18:45"};

  for (int i = 0; i < 3; ++i) {
    int card_y = first_card_y + i * (card_h + card_spacing);

    // Card background (white)
    paint_rect_t card_rect = {card_x, card_y, card_w, card_h};
    paint_FillRect(WHITE, &card_rect);

    // Card border (black)
    paint_rect_t card_border = {card_x, card_y, card_w, card_h};
    paint_FillRect(WHITE, &card_border); // already filled, but for clarity

    // Timestamp text (black, centered in card)
    int ts_width = Font16.Width * (int)strlen(timestamps[i]);
    int ts_x = card_x + (card_w - ts_width) / 2;
    int ts_y = card_y + (card_h - Font16.Height) / 2;
    paint_DrawString(timestamps[i], &Font16, BLACK, ts_x, ts_y);
  }

  // Footer bar (filled white)
  int footer_bar_h = Font12.Height + 12;
  int footer_bar_y = 260;
  paint_rect_t footer_bar = {20, footer_bar_y, 360, footer_bar_h};
  paint_FillRect(WHITE, &footer_bar);

  // Footer text (black, centered)
  const char *footer = "For service, contact: 555-1234";
  int footer_width = Font12.Width * (int)strlen(footer);
  int footer_x = 20 + (360 - footer_width) / 2;
  int footer_y = footer_bar_y + (footer_bar_h - Font12.Height) / 2;
  paint_DrawString(footer, &Font12, BLACK, footer_x, footer_y);

  // --- End Beautiful UI Layout ---

  // Send paint buffers to display
  ssd1683_flush_from_paint(&epd_cfg, wb_buffer, rw_buffer);

  LOG_INF("Display initialized with last cleaned at GUI");

  // Blink LED to show main loop running
  while (1) {
    if (device_is_ready(led.port)) {
      gpio_pin_toggle_dt(&led);
    }
    k_msleep(1000);
  }
  return 0;
}
