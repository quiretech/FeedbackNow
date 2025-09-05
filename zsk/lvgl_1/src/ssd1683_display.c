#include "ssd1683_display.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ssd1683_disp, LOG_LEVEL_INF);

struct ssd1683_display_data {
  const struct ssd1683_config *cfg;
};

static int ssd1683_display_write(const struct device *dev,
                                 const struct display_buffer_descriptor *desc,
                                 const void *buf) {
  struct ssd1683_display_data *data = dev->data;
  const uint8_t *pixels = buf;

  // For simplicity, we just write a full-frame update
  // Could be optimized later for partial updates
  for (int y = 0; y < data->cfg->height; y++) {
    for (int x = 0; x < data->cfg->width; x++) {
      int index = y * data->cfg->width + x;
      uint8_t color = pixels[index] ? SSD1683_COLOR_BLACK : SSD1683_COLOR_WHITE;
      ssd1683_draw_pixel(data->cfg, x, y, color);
    }
  }

  ssd1683_refresh(data->cfg);
  return 0;
}

static int ssd1683_display_get_framebuffer(const struct device *dev,
                                           void **buf) {
  ARG_UNUSED(dev);
  *buf = NULL; // No separate framebuffer in this simple wrapper
  return 0;
}

static const struct display_driver_api ssd1683_driver_api = {
    .write = ssd1683_display_write,
    .get_framebuffer = ssd1683_display_get_framebuffer,
};

static int ssd1683_display_init(const struct device *dev) {
  struct ssd1683_display_data *data = dev->data;

  // initialize hardware via your existing SSD1683 init
  int err = ssd1683_init(data->cfg);
  if (err) {
    return err;
  }

  LOG_INF("SSD1683 Display (Zephyr API) ready");
  return 0;
}

DEVICE_DEFINE(ssd1683_disp, "SSD1683_DISP", ssd1683_display_init, NULL, NULL,
              NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
              &ssd1683_driver_api);
