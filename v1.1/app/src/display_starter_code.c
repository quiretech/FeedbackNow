
// #include <lvgl.h>
// #include <stdlib.h>

// LV_FONT_DECLARE(roboto_28);
// LV_FONT_DECLARE(roboto_36);
// LV_FONT_DECLARE(roboto_bold_42);

// void lvgl_init_display(void) {
//   lv_init();

//   const struct device *display_dev =
//   DEVICE_DT_GET(DT_CHOSEN(zephyr_display)); if
//   (!device_is_ready(display_dev)) {
//     LOG_ERR("Display device not ready");
//     return;
//   }
//   LOG_INF("Display device ready: %s", display_dev->name);

//   // Get active screen
//   lv_obj_t *scr = lv_scr_act();
//   if (!scr) {
//     LOG_ERR("LVGL screen creation failed!");
//     return;
//   }

//   // Set background
//   lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
//   lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

//   // Heading - "last cleaned at:" in Roboto Bold 42, centered at top
//   heading = lv_label_create(scr);
//   lv_label_set_text(heading, "last cleaned at:");
//   lv_obj_set_style_text_font(heading, &roboto_bold_42,
//                              LV_PART_MAIN | LV_STATE_DEFAULT);
//   lv_obj_set_style_text_color(heading, lv_color_black(), LV_PART_MAIN);
//   lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 20);

//   // First horizontal line below heading
//   line1 = lv_line_create(scr);
//   static lv_point_precise_t line1_points[] = {{-80, 0}, {80, 0}};
//   lv_line_set_points(line1, line1_points, 2);
//   lv_obj_set_style_line_width(line1, 1, LV_PART_MAIN);
//   lv_obj_set_style_line_color(line1, lv_color_black(), LV_PART_MAIN);
//   lv_obj_align_to(line1, heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

//   // First timestamp line in Roboto 36
//   timestamp1 = lv_label_create(scr);
//   lv_label_set_text(timestamp1, "2025-10-12 13:25:22");
//   lv_obj_set_style_text_font(timestamp1, &roboto_36,
//                              LV_PART_MAIN | LV_STATE_DEFAULT);
//   lv_obj_set_style_text_color(timestamp1, lv_color_black(), LV_PART_MAIN);
//   lv_obj_align_to(timestamp1, line1, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

//   // Second timestamp line in Roboto 36
//   timestamp2 = lv_label_create(scr);
//   lv_label_set_text(timestamp2, "2025-10-12 18:35:47");
//   lv_obj_set_style_text_font(timestamp2, &roboto_36,
//                              LV_PART_MAIN | LV_STATE_DEFAULT);
//   lv_obj_set_style_text_color(timestamp2, lv_color_black(), LV_PART_MAIN);
//   lv_obj_align_to(timestamp2, timestamp1, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

//   // Second horizontal line to separate timestamps from date
//   line2 = lv_line_create(scr);
//   static lv_point_precise_t line2_points[] = {{-80, 0}, {80, 0}};
//   lv_line_set_points(line2, line2_points, 2);
//   lv_obj_set_style_line_width(line2, 1, LV_PART_MAIN);
//   lv_obj_set_style_line_color(line2, lv_color_black(), LV_PART_MAIN);
//   lv_obj_align_to(line2, timestamp2, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

//   // Date in Roboto 28
//   date_label = lv_label_create(scr);
//   lv_label_set_text(date_label, "2025/11/06");
//   lv_obj_set_style_text_font(date_label, &roboto_28,
//                              LV_PART_MAIN | LV_STATE_DEFAULT);
//   lv_obj_set_style_text_color(date_label, lv_color_black(), LV_PART_MAIN);
//   lv_obj_align_to(date_label, line2, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

//   LOG_INF("LVGL display initialized successfully.");
// }

// while (1) {
//   if (k_msgq_get(&lora_downlink_msgq, rx_buf, K_NO_WAIT) == 0) {
//     /* Defensive: ensure NUL termination even if producer truncated. */
//     rx_buf[sizeof(rx_buf) - 1] = '\0';
//     size_t len = strnlen(rx_buf, sizeof(rx_buf) - 1);

//     // Sanity check
//     if (len < 2) {
//       LOG_WRN("Downlink too short to decode");
//       continue;
//     }

//     if (len % 2 != 0) {
//       LOG_WRN("Odd-length hex string, trimming last nibble");
//       len--;
//     }

//     /* rx_buf max is 63 chars => 31 decoded bytes + NUL */
//     char ascii_str[(sizeof(rx_buf) - 1) / 2 + 1];
//     size_t out_max = sizeof(ascii_str) - 1;
//     size_t out_len = len / 2;
//     if (out_len > out_max) {
//       out_len = out_max;
//     }

//     for (size_t i = 0, oi = 0; i + 1 < len && oi < out_len; i += 2, oi++) {
//       char byte_str[3] = {rx_buf[i], rx_buf[i + 1], '\0'};
//       ascii_str[oi] = (char)strtol(byte_str, NULL, 16);
//     }
//     ascii_str[out_len] = '\0';

//     LOG_INF("Received downlink: %s", ascii_str);

//     // Make all UI changes atomically
//     lv_obj_t *scr = lv_scr_act();

//     // 1. Hide all static UI elements first
//     lv_obj_add_flag(heading, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_add_flag(line1, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_add_flag(timestamp1, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_add_flag(timestamp2, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_add_flag(line2, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_add_flag(date_label, LV_OBJ_FLAG_HIDDEN);

//     // 2. Create and show fullscreen downlink message
//     if (downlink_label) {
//       lv_obj_del(downlink_label);
//       downlink_label = NULL;
//     }
//     downlink_label = lv_label_create(scr);
//     lv_label_set_text(downlink_label, ascii_str);
//     lv_obj_set_style_text_font(downlink_label, &roboto_bold_42,
//                                LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_color(downlink_label, lv_color_black(),
//     LV_PART_MAIN); lv_obj_align(downlink_label, LV_ALIGN_CENTER, 0, 0);

//     // Force complete screen refresh
//     lv_obj_invalidate(scr);

//     LOG_INF("Displayed downlink message fullscreen");

//     request_epd_flush();
//   }

//   if (downlink_restore_pending) {
//     downlink_restore_pending = false;
//     restore_static_display();
//   }

//   /* Handle LVGL tasks. If we have a pending screen change, briefly turn 3V3A
//    * ON so the LVGL flush can complete (SSD1683 write/refresh is
//    synchronous).
//    */
//   if (epd_flush_pending) {
//     /* LoRa may hold 3V3A OFF for RX windows (seconds). Main/UI thread can
//      * block here safely; LoRa runs on its own thread.
//      * EPD shares SPI with SD; serialize so only one uses the bus.
//      */
//     LOG_INF("EPD flush pending; waiting for 3V3A ON...");
//     if (spi_mutex_lock(K_FOREVER) == 0) {
//       (void)power_rail_mgr_require_3v3a_on(POWER_RAIL_CLIENT_EPD, K_FOREVER);
//       LOG_INF("EPD got 3V3A ON; flushing LVGL");
//       lv_task_handler();
//       power_rail_mgr_release_3v3a_on(POWER_RAIL_CLIENT_EPD);
//       spi_mutex_unlock();
//     }
//     epd_flush_pending = false;

//     /* Arm (or restart) restore timer only after the downlink is actually
//      * visible on EPD. */
//     if (downlink_label) {
//       k_timer_stop(&downlink_restore_timer);
//       k_timer_start(&downlink_restore_timer, K_MSEC(DOWNLINK_RESET_DELAY_MS),
//                     K_NO_WAIT);
//     }
//   } else {
//     lv_task_handler();
//   }

//   // Sleep to prevent tight loop
//   k_sleep(K_MSEC(100));
// }
// }