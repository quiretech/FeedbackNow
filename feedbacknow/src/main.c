// #include "heartbeat_work.h"
// #include "led_manager.h"
// #include "lora_app.h"
// #include "lora_manager.h"
// #include "nfc_manager.h"
// #include "state_manager.h"
// #include "sys_config.h"
// #include "system_init.h"
// #include "system_monitor.h"

// #include <lvgl.h>
// #include <zephyr/device.h>
// #include <zephyr/drivers/display.h>
// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>

// LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// // External font declarations
// LV_FONT_DECLARE(roboto_28);
// LV_FONT_DECLARE(roboto_36);
// LV_FONT_DECLARE(roboto_42_multilang);

// lv_obj_t *downlink_label; // Global label pointer
// K_MSGQ_DEFINE(lora_downlink_msgq, 64, 4,
//               4); // holds text payloads (64 bytes max, 4 slots)

// int main(void) {
//   LOG_INF("FeedbackNow System Starting...");

//   // Initialize all subsystems
//   int ret = system_init();
//   if (ret != 0) {
//     LOG_ERR("System initialization failed: %d", ret);
//     return ret;
//   }

//   // Initialize new architecture components
//   ret = state_manager_init();
//   if (ret != 0) {
//     LOG_ERR("State manager initialization failed: %d", ret);
//     return ret;
//   }

//   ret = led_manager_init();
//   if (ret != 0) {
//     LOG_ERR("LED manager initialization failed: %d", ret);
//     return ret;
//   }

//   ret = lora_manager_init();
//   if (ret != 0) {
//     LOG_ERR("LoRa manager initialization failed: %d", ret);
//     return ret;
//   }

//   ret = system_monitor_init();
//   if (ret != 0) {
//     LOG_ERR("System monitor initialization failed: %d", ret);
//     return ret;
//   }

//   // Initialize NFC manager (now done here, not delayed)
//   // ret = nfc_manager_init();
//   // if (ret != 0) {
//   //   LOG_ERR("NFC manager initialization failed: %d", ret);
//   //   return ret;
//   // }

//   // Initialize heartbeat
//   heartbeat_init();

//   // Start the state manager thread
//   LOG_INF("Starting state manager thread...");
//   k_thread_start(state_manager_thread_id);
//   LOG_INF("State manager thread start command issued");

//   // Start the LoRa thread (after LoRaWAN stack is initialized)
//   // Small delay to ensure LoRaWAN stack is fully ready
//   k_sleep(K_MSEC(100));
//   LOG_INF("Starting LoRa thread...");
//   k_thread_start(lora_thread_id);
//   LOG_INF("LoRa thread start command issued");

//   // Start NFC manager thread
//   // k_thread_start(nfc_manager_thread_id);
//   // LOG_INF("NFC manager thread start command issued");

//   const struct device *display_dev =
//   DEVICE_DT_GET(DT_CHOSEN(zephyr_display)); if
//   (!device_is_ready(display_dev)) {
//     LOG_ERR("Display device not ready");
//     return -ENODEV;
//   }
//   LOG_INF("Display device ready: %s", display_dev->name);

//   // Display info
//   struct display_capabilities caps;
//   display_get_capabilities(display_dev, &caps);
//   LOG_INF("Display: %dx%d, format=%d", caps.x_resolution, caps.y_resolution,
//           caps.current_pixel_format);

//   // Get the active screen (don't create a new one)
//   lv_obj_t *scr = lv_scr_act();

//   // IMPORTANT: For monochrome EPD, set background to WHITE (1)
//   lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
//   lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

//   // Heading - Using roboto_28 (closest to 24 we have)
//   lv_obj_t *heading = lv_label_create(scr);
//   lv_label_set_text(heading, "last cleaned at:");
//   lv_obj_set_style_text_font(heading, &roboto_42_multilang,
//                              LV_PART_MAIN | LV_STATE_DEFAULT);
//   lv_obj_set_style_text_color(heading, lv_color_black(), LV_PART_MAIN);
//   lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 20);
//   // Add a horizontal line below the heading using LVGL
//   lv_obj_t *line = lv_line_create(scr);
//   static lv_point_t line_points[] = {{-80, 0}, {80, 0}}; // 160px wide
//   centered lv_line_set_points(line, line_points, 2);
//   lv_obj_set_style_line_width(line, 2,
//                               LV_PART_MAIN); // adjust thickness as needed
//   lv_obj_set_style_line_color(line, lv_color_black(), LV_PART_MAIN);
//   lv_obj_align_to(line, heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

//   // Line 1 - roboto_36
//   lv_obj_t *line1 = lv_label_create(scr);
//   lv_label_set_text(line1, "2025-10-12 13:25:22");
//   lv_obj_set_style_text_font(line1, &roboto_36,
//                              LV_PART_MAIN | LV_STATE_DEFAULT);
//   lv_obj_set_style_text_color(line1, lv_color_black(), LV_PART_MAIN);
//   lv_obj_align(line1, LV_ALIGN_CENTER, 0, -30);

//   // // Line 2 - roboto_36
//   // lv_obj_t *line2 = lv_label_create(scr);
//   // lv_label_set_text(line2, "2025-10-12 18:35:47");
//   // lv_obj_set_style_text_font(line2, &roboto_36,
//   //                            LV_PART_MAIN | LV_STATE_DEFAULT);
//   // lv_obj_set_style_text_color(line2, lv_color_black(), LV_PART_MAIN);
//   // lv_obj_align(line2, LV_ALIGN_CENTER, 0, 30);

//   // // Line 3 - roboto_36
//   // lv_obj_t *line3 = lv_label_create(scr);
//   // lv_label_set_text(line3, "2025-10-12 19:25:37");
//   // lv_obj_set_style_text_font(line3, &roboto_36,
//   //                            LV_PART_MAIN | LV_STATE_DEFAULT);
//   // lv_obj_set_style_text_color(line3, lv_color_black(), LV_PART_MAIN);
//   // lv_obj_align(line3, LV_ALIGN_CENTER, 0, 90);
//   // Downlink data label
//   downlink_label = lv_label_create(scr);
//   lv_label_set_text(downlink_label, "awaiting downlink...");
//   lv_obj_set_style_text_font(downlink_label, &roboto_36,
//                              LV_PART_MAIN | LV_STATE_DEFAULT);
//   lv_obj_set_style_text_color(downlink_label, lv_color_black(),
//   LV_PART_MAIN); lv_obj_align(downlink_label, LV_ALIGN_CENTER, 0, 60);
//   // ========================================================================

//   // Send system ready event
//   system_event_msg_t ready_event = {.event_type = EVENT_SYSTEM_READY};
//   state_manager_send_event(&ready_event);

//   LOG_INF("System initialization complete. All threads started.");
//   LOG_INF("System ready for user input.");

//   // Log thread status
//   LOG_INF("=== THREAD STATUS CHECK ===");
//   LOG_INF("Main thread ID: %p", k_current_get());
//   LOG_INF("State manager thread ID: %p", state_manager_thread_id);
//   LOG_INF("LoRa thread ID: %p", lora_thread_id);
//   LOG_INF("NFC manager thread ID: %p", nfc_manager_thread_id);

//   // Thread status logged above

//   // Main loop - monitor system health and update LVGL
//   int loop_count = 0;
//   char rx_buf[64];
//   while (1) {
//     if (k_msgq_get(&lora_downlink_msgq, rx_buf, K_NO_WAIT) == 0) {
//       LOG_INF("Received LoRa downlink text: %s", rx_buf);
//       lv_label_set_text(downlink_label, rx_buf);
//     }
//     // Update LVGL periodically
//     lv_task_handler();

//     // Small sleep to prevent tight loop
//     k_sleep(K_MSEC(100));

//     // Periodic thread health check
//     if (loop_count % 100 == 0) { // Every ~10 seconds
//       LOG_INF("Uptime: %llu ms", k_uptime_get());
//     }
//     loop_count++;
//   }
// }

/// TEST ////
#include "heartbeat_work.h"
#include "led_manager.h"
#include "lora_app.h"
#include "lora_manager.h"
#include "nfc_manager.h"
#include "power_ctrl.h"
#include "state_manager.h"
#include "sys_config.h"
#include "system_init.h"
#include "system_monitor.h"

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// External fonts
LV_FONT_DECLARE(roboto_28);
LV_FONT_DECLARE(roboto_36);
LV_FONT_DECLARE(roboto_bold_42);
LV_FONT_DECLARE(notokufiarabic_32);

// LoRa downlink message queue
K_MSGQ_DEFINE(lora_downlink_msgq, 64, 4, 4);

// Global pointers to static UI elements
static lv_obj_t *heading = NULL;
static lv_obj_t *line1 = NULL;
static lv_obj_t *timestamp1 = NULL;
static lv_obj_t *timestamp2 = NULL;
static lv_obj_t *line2 = NULL;
static lv_obj_t *date_label = NULL;

// Downlink UI elements
lv_obj_t *downlink_label = NULL; // Global pointer for downlink message
static lv_timer_t *downlink_reset_timer = NULL; // Timer handle

#define DOWNLINK_RESET_DELAY_MS 3000

// ================= Callback: Restore static display =================
static void restore_static_display_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);

  // Make all changes atomically
  // 1. Delete downlink label first
  if (downlink_label) {
    lv_obj_del(downlink_label);
    downlink_label = NULL;
  }

  // 2. Show all static elements
  lv_obj_clear_flag(heading, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(line1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(timestamp1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(timestamp2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(line2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(date_label, LV_OBJ_FLAG_HIDDEN);

  // Force complete screen refresh
  lv_obj_t *scr = lv_scr_act();
  lv_obj_invalidate(scr);

  LOG_INF("Static display restored");
}

void lvgl_init_display(void) {
  lv_init();

  const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display_dev)) {
    LOG_ERR("Display device not ready");
    return;
  }
  LOG_INF("Display device ready: %s", display_dev->name);

  // Get active screen
  lv_obj_t *scr = lv_scr_act();
  if (!scr) {
    LOG_ERR("LVGL screen creation failed!");
    return;
  }

  // Set background
  lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  // Heading - "last cleaned at:" in Roboto Bold 42, centered at top
  heading = lv_label_create(scr);
  lv_label_set_text(heading, "last cleaned at:");
  lv_obj_set_style_text_font(heading, &roboto_bold_42,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(heading, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 20);

  // First horizontal line below heading
  line1 = lv_line_create(scr);
  static lv_point_t line1_points[] = {{-80, 0}, {80, 0}};
  lv_line_set_points(line1, line1_points, 2);
  lv_obj_set_style_line_width(line1, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(line1, lv_color_black(), LV_PART_MAIN);
  lv_obj_align_to(line1, heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // First timestamp line in Roboto 36
  timestamp1 = lv_label_create(scr);
  lv_label_set_text(timestamp1, "2025-10-12 13:25:22");
  lv_obj_set_style_text_font(timestamp1, &roboto_36,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(timestamp1, lv_color_black(), LV_PART_MAIN);
  lv_obj_align_to(timestamp1, line1, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

  // Second timestamp line in Roboto 36
  timestamp2 = lv_label_create(scr);
  lv_label_set_text(timestamp2, "2025-10-12 18:35:47");
  lv_obj_set_style_text_font(timestamp2, &roboto_36,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(timestamp2, lv_color_black(), LV_PART_MAIN);
  lv_obj_align_to(timestamp2, timestamp1, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // Second horizontal line to separate timestamps from date
  line2 = lv_line_create(scr);
  static lv_point_t line2_points[] = {{-80, 0}, {80, 0}};
  lv_line_set_points(line2, line2_points, 2);
  lv_obj_set_style_line_width(line2, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(line2, lv_color_black(), LV_PART_MAIN);
  lv_obj_align_to(line2, timestamp2, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

  // Date in Roboto 28
  date_label = lv_label_create(scr);
  lv_label_set_text(date_label, "2025/11/06");
  lv_obj_set_style_text_font(date_label, &roboto_28,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(date_label, lv_color_black(), LV_PART_MAIN);
  lv_obj_align_to(date_label, line2, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

  LOG_INF("LVGL display initialized successfully.");
}

// ================= Main Application =================
int main(void) {
  LOG_INF("FeedbackNow System Starting...");

  // ===== System Initialization =====
  if (system_init() != 0) {
    LOG_ERR("System initialization failed");
    return -1;
  }

  // Initialize power control and enable all rails
  if (power_ctrl_init() != 0) {
    LOG_ERR("Power control initialization failed");
    return -1;
  }

  // Enable all power rails
  power_ctrl_set(POWER_EN_3V3, true);
  power_ctrl_set(POWER_EN_1V8, true);
  power_ctrl_set(POWER_EN_3V3A, false);
  power_ctrl_set(POWER_EN_3V6, true);
  LOG_INF("All power rails enabled");

  state_manager_init();
  led_manager_init();
  lora_manager_init();
  system_monitor_init();
  heartbeat_init();

  // Start threads safely
  k_thread_start(state_manager_thread_id);
  k_sleep(K_MSEC(100)); // Small delay for LoRa stack
  k_thread_start(lora_thread_id);

  // Initialize LVGL display after system init
  lvgl_init_display();

  // Send system ready event
  system_event_msg_t ready_event = {.event_type = EVENT_SYSTEM_READY};
  state_manager_send_event(&ready_event);

  LOG_INF("System ready. Entering main loop...");

  // ===== Main Loop =====
  char rx_buf[64];
#define LORA_MSG_MAX 63 // leave 1 byte for null terminator

  while (1) {
    if (k_msgq_get(&lora_downlink_msgq, rx_buf, K_NO_WAIT) == 0) {
      size_t len = strlen(rx_buf);

      // Sanity check
      if (len < 2) {
        LOG_WRN("Downlink too short to decode");
        continue;
      }

      if (len % 2 != 0) {
        LOG_WRN("Odd-length hex string, trimming last nibble");
        len--;
      }

      char ascii_str[len / 2 + 1];
      for (size_t i = 0; i < len; i += 2) {
        char byte_str[3] = {rx_buf[i], rx_buf[i + 1], '\0'};
        ascii_str[i / 2] = (char)strtol(byte_str, NULL, 16);
      }
      ascii_str[len / 2] = '\0';

      LOG_INF("Received downlink: %s", ascii_str);

      // Make all UI changes atomically
      lv_obj_t *scr = lv_scr_act();

      // 1. Hide all static UI elements first
      lv_obj_add_flag(heading, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(line1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(timestamp1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(timestamp2, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(line2, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(date_label, LV_OBJ_FLAG_HIDDEN);

      // 2. Create and show fullscreen downlink message
      downlink_label = lv_label_create(scr);
      lv_label_set_text(downlink_label, ascii_str);
      lv_obj_set_style_text_font(downlink_label, &notokufiarabic_32,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_text_color(downlink_label, lv_color_black(),
                                  LV_PART_MAIN);
      lv_obj_align(downlink_label, LV_ALIGN_CENTER, 0, 0);

      // Force complete screen refresh
      lv_obj_invalidate(scr);

      LOG_INF("Displayed downlink message fullscreen");

      // Create a one-shot timer to restore static display after delay
      downlink_reset_timer = lv_timer_create(restore_static_display_cb,
                                             DOWNLINK_RESET_DELAY_MS, NULL);
      lv_timer_set_repeat_count(downlink_reset_timer, 1);
    }

    // Handle LVGL tasks
    lv_task_handler();

    // Sleep to prevent tight loop
    k_sleep(K_MSEC(100));
  }
}
