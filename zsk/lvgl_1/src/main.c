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

  // Clear screen
  ssd1683_clear(&epd_cfg);

  ssd1683_draw_rect_fb(46, 31, 31, 235, SSD1683_COLOR_WHITE);
  ssd1683_draw_rect_fb(86, 31, 31, 235, SSD1683_COLOR_WHITE);
  ssd1683_draw_rect_fb(126, 31, 31, 235, SSD1683_COLOR_WHITE);
  ssd1683_draw_rect_fb(246, 31, 31, 235, SSD1683_COLOR_WHITE);
  ssd1683_draw_rect_fb(286, 31, 31, 235, SSD1683_COLOR_WHITE);
  ssd1683_draw_rect_fb(326, 31, 31, 235, SSD1683_COLOR_WHITE);

  ssd1683_draw_rect_fb(58, 85, 9, 130, SSD1683_COLOR_RED);
  ssd1683_draw_rect_fb(137, 85, 9, 130, SSD1683_COLOR_RED);
  ssd1683_draw_rect_fb(257, 85, 9, 130, SSD1683_COLOR_RED);
  ssd1683_draw_rect_fb(337, 85, 9, 130, SSD1683_COLOR_RED);
  ssd1683_draw_rect_fb(176, 85, 54, 130, SSD1683_COLOR_RED);
  //   //   ssd1683_draw_string_fb(10, 20, "Quick Brown Fox Jumps Over The Lazy
  //   Dog",
  //   //                          SSD1683_COLOR_RED);
  //   //   ssd1683_draw_string_fb(10, 40, "1234567890~!@#$%^&*()_+=",
  //   //   SSD1683_COLOR_RED);

  //   ssd1683_draw_bitmap(&epd_cfg); // draw your header image
  ssd1683_flush(&epd_cfg);

  ssd1683_deep_sleep(&epd_cfg);
  LOG_INF("Display initialized and circle drawn");

  // Blink LED to show main loop running
  while (1) {
    if (device_is_ready(led.port)) {
      gpio_pin_toggle_dt(&led);
    }
    k_msleep(1000);
  }
  return 0;
}
