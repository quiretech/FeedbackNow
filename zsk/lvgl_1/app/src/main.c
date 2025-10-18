/*
 * SSD1683 E-Paper Display with Custom Font Rendering
 * Displays "Hello World" using custom font rendering
 */

#include "custom_font.h"
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

  // Wait a moment for display to be ready
  k_sleep(K_SECONDS(1));

  // Render "Hello World" text
  LOG_INF("Rendering Hello World with custom font...");
  int ret = custom_font_render_text(display_dev, "Hello World", 50, 100);
  if (ret < 0) {
    LOG_ERR("Failed to render text: %d", ret);
    return ret;
  }

  LOG_INF("Hello World rendered successfully!");

  // Keep running
  while (1) {
    k_sleep(K_SECONDS(10));
    LOG_INF("System running...");
  }
}