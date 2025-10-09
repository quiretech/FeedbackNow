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

  // Draw two red boxes
  ssd1683_draw_rect_fb(20, 20, 100, 60, SSD1683_COLOR_RED);
  ssd1683_draw_rect_fb(150, 100, 120, 80, SSD1683_COLOR_RED);

  // Draw red text inside the boxes
  ssd1683_draw_string_fb(30, 40, "Red Box 1", SSD1683_COLOR_BLACK);
  ssd1683_draw_string_fb(160, 130, "Red Box 2", SSD1683_COLOR_BLACK);

  // Flush framebuffer to display
  ssd1683_flush(&epd_cfg);

  LOG_INF("Display initialized, red boxes and text drawn");

  // Blink LED to show main loop running
  while (1) {
    if (device_is_ready(led.port)) {
      gpio_pin_toggle_dt(&led);
    }
    k_msleep(1000);
  }
  return 0;
}
