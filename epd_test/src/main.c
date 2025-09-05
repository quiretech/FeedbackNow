#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(epd_test_main, LOG_LEVEL_INF);

void main(void) {
  const struct device *epd = DEVICE_DT_GET(DT_NODELABEL(epd_ssd1683));

  if (!device_is_ready(epd)) {
    LOG_ERR("EPD device not ready!");
    return;
  }

  LOG_INF("EPD device initialized successfully");

  /* Get display capabilities */
  struct display_capabilities caps;
  display_get_capabilities(epd, &caps);
  LOG_INF("Display: %dx%d, format: %d", caps.x_resolution, caps.y_resolution,
          caps.current_pixel_format);

  /* Create a simple test pattern */
  uint8_t test_pattern[400 * 300];
  for (int i = 0; i < 400 * 300; i++) {
    test_pattern[i] = (i % 2) ? 1 : 0; /* Checkerboard pattern */
  }

  /* Write test pattern to display */
  int ret = display_write(epd, 0, 0, 400, 300, test_pattern, 0, 0);
  if (ret) {
    LOG_ERR("Failed to write to display: %d", ret);
  } else {
    LOG_INF("Test pattern written to display");
  }

  while (1) {
    LOG_INF("EPD test running...");
    k_msleep(5000);
  }
}
