/**
 * @file main.c
 * @brief Simple uptime display for SSD1683 E-Paper Display
 */

#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

/* Font declaration */
LV_FONT_DECLARE(roboto_36);

int main(void) {
  const struct device *display;
  lv_obj_t *uptime_label;
  lv_style_t style;
  uint32_t seconds = 0;
  char buf[32];

  /* Initialize display */
  display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display)) {
    printk("Error: display not ready\n");
    return -1;
  }

  /* Setup style */
  lv_style_init(&style);
  lv_style_set_text_font(&style, &roboto_36);
  lv_style_set_text_color(&style, lv_color_black());

  /* Create centered label */
  uptime_label = lv_label_create(lv_scr_act());
  lv_obj_add_style(uptime_label, &style, 0);
  lv_label_set_text(uptime_label, "Uptime: 0");
  lv_obj_center(uptime_label);

  display_blanking_off(display);

  /* Main loop - update every 5 seconds */
  while (1) {
    snprintf(buf, sizeof(buf), "Counter: %u", seconds);
    lv_label_set_text(uptime_label, buf);
    lv_task_handler();

    k_msleep(1000);
    seconds += 1;
  }

  return 0;
}
