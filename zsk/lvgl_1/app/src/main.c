/*
 * SSD1683 E-Paper Display + LVGL
 * Sensor Dashboard Demo (with dummy data)
 * Based on LVGL sensor display example
 */

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LV_FONT_DECLARE(roboto_20);
LV_FONT_DECLARE(roboto_28);

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

  /* Initialize LVGL (Zephyr auto-inits, but let's be explicit) */
  LOG_INF("Initializing LVGL...");

  /* Wait a bit for LVGL to be fully ready */
  k_sleep(K_MSEC(100));

  /* Set display to blanking mode to batch initial UI creation */
  display_blanking_on(display_dev);
  LOG_INF("Display blanking ON - batching UI updates");

  /* Create GUI */
  lv_obj_t *sensor_label, *value_label;
  lv_obj_t *t_label, *h_label, *g_label, *v_label;
  lv_obj_t *t_val_label, *h_val_label, *g_val_label, *v_val_label;
  char num_buf[32];

  /* Create labels */
  sensor_label = lv_label_create(lv_scr_act());
  value_label = lv_label_create(lv_scr_act());
  t_label = lv_label_create(lv_scr_act());
  h_label = lv_label_create(lv_scr_act());
  g_label = lv_label_create(lv_scr_act());
  v_label = lv_label_create(lv_scr_act());
  t_val_label = lv_label_create(lv_scr_act());
  h_val_label = lv_label_create(lv_scr_act());
  g_val_label = lv_label_create(lv_scr_act());
  v_val_label = lv_label_create(lv_scr_act());

  /* Set fonts for header labels */
  lv_obj_set_style_text_font(sensor_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(value_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);

  /* Set fonts for data labels */
  lv_obj_set_style_text_font(t_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(h_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(g_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(v_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(t_val_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(h_val_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(g_val_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(v_val_label, &roboto_20,
                             LV_PART_MAIN | LV_STATE_DEFAULT);

  /* Position labels - headers at top */
  lv_obj_align(sensor_label, LV_ALIGN_TOP_LEFT, 10, 20);
  lv_obj_align(value_label, LV_ALIGN_TOP_MID, 50, 20);

  /* Position sensor name labels */
  lv_obj_align_to(t_label, sensor_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);
  lv_obj_align_to(h_label, t_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
  lv_obj_align_to(g_label, h_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
  lv_obj_align_to(v_label, g_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

  /* Position value labels */
  lv_obj_align_to(t_val_label, value_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);
  lv_obj_align_to(h_val_label, t_val_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
  lv_obj_align_to(g_val_label, h_val_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
  lv_obj_align_to(v_val_label, g_val_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

  /* Set static header text */
  lv_label_set_text(sensor_label, "Sensor");
  lv_label_set_text(value_label, "Value");
  lv_label_set_text(t_label, "Temp");
  lv_label_set_text(h_label, "Humidity");
  lv_label_set_text(g_label, "Gas");
  lv_label_set_text(v_label, "VOC");

  /* Set initial values */
  lv_label_set_text(t_val_label, "22.5 C");
  lv_label_set_text(h_val_label, "45.0 %");
  lv_label_set_text(g_val_label, "25000");
  lv_label_set_text(v_val_label, "100");

  /* Process LVGL updates while blanking is ON (batched) */
  LOG_INF("Processing initial UI render...");
  lv_task_handler();

  /* Now turn blanking OFF to display everything */
  LOG_INF("Display blanking OFF - flushing to screen");
  display_blanking_off(display_dev);

  LOG_INF("Initial render complete. Starting sensor update loop...");
  k_sleep(K_MSEC(2000));

  int counter = 0;
  float temp = 22.5;
  float hum = 45.0;
  int gas = 25000;
  int voc_index = 100;

  /* Main loop */
  while (1) {
    /* Simulate sensor readings with slight variations */
    temp += (counter % 2 == 0) ? 0.1 : -0.1;
    hum += (counter % 3 == 0) ? 0.5 : -0.3;
    gas += (counter % 5 == 0) ? 100 : -50;
    voc_index += (counter % 4 == 0) ? 5 : -3;

    /* Keep values in reasonable range */
    if (temp < 20.0)
      temp = 22.5;
    if (temp > 25.0)
      temp = 22.5;
    if (hum < 40.0)
      hum = 45.0;
    if (hum > 50.0)
      hum = 45.0;
    if (gas < 24000)
      gas = 25000;
    if (gas > 26000)
      gas = 25000;
    if (voc_index < 80)
      voc_index = 100;
    if (voc_index > 120)
      voc_index = 100;

    /* Update value labels */
    snprintf(num_buf, sizeof(num_buf), "%.1f C", temp);
    lv_label_set_text(t_val_label, num_buf);

    snprintf(num_buf, sizeof(num_buf), "%.1f %%", hum);
    lv_label_set_text(h_val_label, num_buf);

    snprintf(num_buf, sizeof(num_buf), "%d", gas);
    lv_label_set_text(g_val_label, num_buf);

    snprintf(num_buf, sizeof(num_buf), "%d", voc_index);
    lv_label_set_text(v_val_label, num_buf);

    /* Full refresh every 20 updates */
    counter++;
    if (!(counter % 20)) {
      LOG_INF("Full refresh at counter=%d", counter);
      display_blanking_on(display_dev);
      display_blanking_off(display_dev);
    }

    lv_task_handler();
    k_sleep(K_MSEC(1000));
  }

  return 0;
}
