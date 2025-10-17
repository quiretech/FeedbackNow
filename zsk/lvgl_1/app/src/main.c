/*
 * SSD1683 E-Paper Display + LVGL
 * Simple Counter Demo
 * Displays "counter = X" in the center of screen and updates every second
 */

#include "test_partial.h"
// #include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void) {
  const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

  /* Check if display device is ready */
  if (!device_is_ready(display_dev)) {
    LOG_ERR("Display device not ready");
    return -ENODEV;
  }
  LOG_INF("Display device ready");

  /* Get display info */
  struct display_capabilities caps;
  display_get_capabilities(display_dev, &caps);
  LOG_INF("Display: %dx%d, format: %d", caps.x_resolution, caps.y_resolution,
          caps.current_pixel_format);

  while (1) {
    // Test CFB text rendering with single font
    test_cfb_text(display_dev);
    k_sleep(K_SECONDS(3));

    // Test CFB with multiple fonts
    test_cfb_multiple_fonts(display_dev);
    k_sleep(K_SECONDS(3));

    // Test partial refresh
    test_partial_refresh_direct(display_dev);
    k_sleep(K_SECONDS(3));
  }
}
