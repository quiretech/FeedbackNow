/*
 * SSD1683 E-Paper Display + LVGL
 * Cleaning Status Dashboard
 */

#include <lvgl.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LV_FONT_DECLARE(roboto_20);
LV_FONT_DECLARE(roboto_28);
LV_FONT_DECLARE(roboto_32);
LV_FONT_DECLARE(roboto_36);
LV_FONT_DECLARE(roboto_42);
LV_FONT_DECLARE(roboto_bold_42);

LOG_MODULE_REGISTER(cleaning_dashboard, LOG_LEVEL_INF);

int main(void) {
  const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

  /* Check if display device is ready */
  if (!device_is_ready(display)) {
    LOG_ERR("Display device not ready");
    return -ENODEV;
  }
  LOG_INF("Display device ready");

  /* Get display info */
  struct display_capabilities caps;
  display_get_capabilities(display, &caps);
  LOG_INF("Display: %dx%d, format: %d", caps.x_resolution, caps.y_resolution,
          caps.current_pixel_format);

  /* Create GUI */
  // Display the phrase in three different Roboto fonts using LVGL, vertically
  // stacked
  lv_obj_t *scr = lv_scr_act();

  static const char *msg = "hello world";

  // Label for roboto_42

  // Label for roboto_42 (largest, on top)
  lv_obj_t *label42 = lv_label_create(scr);
  lv_label_set_text(label42, msg);
  lv_obj_set_style_text_font(label42, &roboto_bold_42,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(label42, LV_ALIGN_TOP_MID, 0, 10);

  // Label for roboto_36 (next largest, under 42)
  lv_obj_t *label36 = lv_label_create(scr);
  lv_label_set_text(label36, msg);
  lv_obj_set_style_text_font(label36, &roboto_42,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(label36, label42, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // Label for roboto_32 (middle, under 36)
  lv_obj_t *label32 = lv_label_create(scr);
  lv_label_set_text(label32, msg);
  lv_obj_set_style_text_font(label32, &roboto_32,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(label32, label36, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // Label for roboto_28 (next, under 32)
  lv_obj_t *label28 = lv_label_create(scr);
  lv_label_set_text(label28, msg);
  lv_obj_set_style_text_font(label28, &roboto_28,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(label28, label32, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // Label for roboto_20 (smallest, on the bottom)
  lv_obj_t *label20 = lv_label_create(scr);
  lv_label_set_text(label20, msg);
  lv_obj_set_style_text_font(label20, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(label20, label28, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  /* Main loop */
  LOG_INF("Entering main loop...");
  while (1) {
    lv_task_handler();
    k_sleep(K_MSEC(100));
  }

  return 0;
}
