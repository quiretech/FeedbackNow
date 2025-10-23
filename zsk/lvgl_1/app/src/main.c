#include "ssd1683.h"
#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

LV_FONT_DECLARE(roboto_36);

static lv_obj_t *counter_label;

// Display buffer for LVGL - allocate ~10% of screen (400x30 pixels = 1500
// bytes)
#define BUFFER_HEIGHT 30
static uint8_t draw_buf[SSD1683_WIDTH * BUFFER_HEIGHT / 8];

// Tick timer for LVGL
static void lv_tick_cb(struct k_timer *dummy) { lv_tick_inc(1); }
K_TIMER_DEFINE(lv_tick_timer, lv_tick_cb, NULL);

// Flush callback for LVGL 9 + Zephyr
static void lv_flush_cb(lv_display_t *disp, const lv_area_t *area,
                        unsigned char *color_map) {
  const struct device *dev =
      (const struct device *)lv_display_get_user_data(disp);

  uint16_t w = area->x2 - area->x1 + 1;
  uint16_t h = area->y2 - area->y1 + 1;

  struct display_buffer_descriptor desc = {
      .buf_size = ((w + 7) / 8) * h, // width in bytes * height
      .width = w,
      .height = h,
      .pitch = (w + 7) / 8, // bytes per row
  };

  display_write(dev, area->x1, area->y1, &desc, color_map);

  lv_display_flush_ready(disp);
}

int main(void) {
  LOG_INF("=== INITIALIZING LVGL DISPLAY ===");

  const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display_dev)) {
    LOG_ERR("Display device not ready");
    return -ENODEV;
  }

  LOG_INF("Display device ready: %s", display_dev->name);

  // Initialize LVGL
  lv_init();

  // Create LVGL display object for SSD1683 size (400x300 monochrome)
  lv_display_t *disp = lv_display_create(SSD1683_WIDTH, SSD1683_HEIGHT);
  if (disp == NULL) {
    LOG_ERR("Failed to create LVGL display");
    return -ENOMEM;
  }

  // Set display buffer
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  // Set color format to 1-bit monochrome
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);

  // Set flush callback and user data
  lv_display_set_flush_cb(disp, lv_flush_cb);
  lv_display_set_user_data(disp, (void *)display_dev);

  // Start LVGL tick timer (1ms tick)
  // k_timer_start(&lv_tick_timer, K_MSEC(1), K_MSEC(1));

  LOG_INF("LVGL initialized successfully");

  // Create UI
  lv_obj_t *scr = lv_scr_act();

  // Set background to white for e-paper
  lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  // Create counter label
  counter_label = lv_label_create(scr);
  lv_label_set_text(counter_label, "Counter: 0");
  lv_obj_set_style_text_font(counter_label, &roboto_36, LV_PART_MAIN);
  lv_obj_set_style_text_color(counter_label, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 0);

  LOG_INF("UI created, starting counter");

  int counter = 0;
  while (1) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Counter: %d", counter++);
    lv_label_set_text(counter_label, buf);

    lv_task_handler(); // process LVGL tasks and flush buffer
    k_sleep(K_SECONDS(100));
  }
}
