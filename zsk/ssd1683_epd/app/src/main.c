/**
 * @file main.c
 * @brief Simple uptime display for SSD1683 E-Paper Display
 */

#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

#include "power_ctrl.h"

#define CLEAR_SCREEN

#ifdef CLEAR_SCREEN
#include "ssd1683.h"
#endif

/* Font declaration */
LV_FONT_DECLARE(roboto_36);

int main(void) {
  const struct device *display;
  lv_obj_t *uptime_label;
  lv_style_t style;
  uint32_t seconds = 0;
  char buf[32];
  int ret;

  /* Initialize power control */
  ret = power_ctrl_init();
  if (ret != 0) {
    printk("Error: power_ctrl_init failed (%d)\n", ret);
    return ret;
  }

  /* Enable all power rails */
  power_ctrl_set(POWER_EN_3V3, true);
  power_ctrl_set(POWER_EN_1V8, true);
  power_ctrl_set(POWER_EN_3V3A, true);
  power_ctrl_set(POWER_EN_3V6, true);

  /* Allow power rails to stabilize */
  k_msleep(10);

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
  lv_label_set_text(uptime_label, "Count: 0");
  lv_obj_center(uptime_label);

  display_blanking_off(display);
#ifdef CLEAR_SCREEN
  ssd1683_clear_screen(display, 0xFF);
#else
  while (1) {
    snprintf(buf, sizeof(buf), "Count: %u", seconds);
    lv_label_set_text(uptime_label, buf);
    lv_task_handler();
    k_msleep(1000);
    seconds += 1;
  }
#endif
  return 0;
}
