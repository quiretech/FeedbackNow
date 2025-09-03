#include "C:\Users\jatan\Desktop\githubrepo\fb_now\epd_test\drivers\display\ssd1683\include\ssd1683_epd.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(epd_test_main, LOG_LEVEL_INF);

void main(void) {
  const struct device *epd = DEVICE_DT_GET(DT_ALIAS(epd0));

  if (!device_is_ready(epd)) {
    LOG_ERR("EPD device not ready!");
    return;
  }

  LOG_INF("EPD device initialized successfully");

  const struct ssd1683_epd_config *cfg = epd->config;

  /* Example: toggle reset manually again if needed */
  gpio_pin_set(cfg->reset_dev, cfg->reset_pin, 0);
  k_msleep(10);
  gpio_pin_set(cfg->reset_dev, cfg->reset_pin, 1);
  k_msleep(10);

  while (1) {
    LOG_INF("EPD test running...");
    k_msleep(1000);
  }
}
