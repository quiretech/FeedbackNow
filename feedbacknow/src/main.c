
// // #include "buttons.h"
// // #include "leds.h"
// // #include "nvs.h"
// // #include "system_init.h"
// // #include <zephyr/kernel.h>
// // #include <zephyr/logging/log.h>

// // LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// // int main(void) {
// //   system_init();

// //   while (1) {
// //     k_sleep(K_SECONDS(1));
// //   }
// // }

// #include <zephyr/device.h>
// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>

// #include <zephyr/drivers/flash.h>
// #include <zephyr/fs/nvs.h>
// #include <zephyr/lorawan/lorawan.h>
// #include <zephyr/storage/flash_map.h>

// #include <zephyr/drivers/gpio.h>

// #include "buttons.h"
// #include "lora_app.h"
// #include "nvs.h"

// #define DELAY K_MSEC(10000)
// #define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL

// // #define NVS_LORAWAN_KEYS

// LOG_MODULE_REGISTER(main);

// char data[] = {'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd'};

// static void dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t
// snr,
//                         uint8_t len, const uint8_t *hex_data) {
//   LOG_INF("Port %d, Pending %d, RSSI %ddB, SNR %ddBm, Time %d", port,
//           flags & LORAWAN_DATA_PENDING, rssi, snr,
//           !!(flags & LORAWAN_TIME_UPDATED));
//   if (hex_data) {
//     LOG_HEXDUMP_INF(hex_data, len, "Payload: ");
//   }
// }

// static void lorwan_datarate_changed(enum lorawan_datarate dr) {
//   uint8_t unused, max_size;

//   lorawan_get_payload_sizes(&unused, &max_size);
//   LOG_INF("New Datarate: DR_%d, Max Payload %d", dr, max_size);
// }

// int main(void) {
//   system_init();
//   int ret;
//   uint16_t dev_nonce = 0;
//   ssize_t bytes_written;

// #ifdef NVS_LORAWAN_KEYS
//   uint8_t dev_eui[8];
//   uint8_t join_eui[8];
//   uint8_t app_key[16];
// #else
//   uint8_t dev_eui[] = LORAWAN_DEV_EUI;
//   uint8_t join_eui[] = LORAWAN_JOIN_EUI;
//   uint8_t app_key[] = LORAWAN_APP_KEY;
// #endif

// #ifdef NVS_LORAWAN_KEYS
//   nvs_manager_read_or_generate(NVS_LORAWAN_DEV_EUI_ID, dev_eui,
//                                sizeof(dev_eui));
//   nvs_manager_read_or_generate(NVS_LORAWAN_JOIN_EUI_ID, join_eui,
//                                sizeof(join_eui));
//   nvs_manager_read_or_generate(NVS_LORAWAN_APP_KEY_ID, app_key,
//                                sizeof(app_key));
// #endif

//   // Init Lorawan Device & Functions

//   const struct device *lora_dev;
//   struct lorawan_join_config join_cfg;
//   struct lorawan_downlink_cb downlink_cb = {.port = LW_RECV_PORT_ANY,
//                                             .cb = dl_callback};
//   lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
//   if (!device_is_ready(lora_dev)) {
//     LOG_ERR("%s: device not ready.", lora_dev->name);
//     return 0;
//   } else {
//     LOG_INF("%s: device ready.", lora_dev->name);
//   }

// #if defined(CONFIG_LORAMAC_REGION_EU868)
//   ret = lorawan_set_region(LORAWAN_REGION_EU868);
//   if (ret < 0) {
//     LOG_ERR("lorawan_set_region failed: %d", ret);
//     return 0;
//   }
// #endif

//   ret = lorawan_start();
//   if (ret < 0) {
//     LOG_ERR("lorawan_start failed: %d", ret);
//     return 0;
//   }

//   lorawan_register_downlink_callback(&downlink_cb);
//   lorawan_register_dr_changed_callback(lorwan_datarate_changed);

//   // Read keys and dev_nonce from NVS using nvs_manager_read
//   ret = nvs_manager_read(NVS_DEVNONCE_ID, &dev_nonce, sizeof(dev_nonce));
//   if (ret >= 0) {
//     LOG_INF("Read dev_nonce from NVS: %d", dev_nonce);
//   } else {
//     LOG_INF("Dev nonce not found in NVS, using 0");
//     dev_nonce = 0;
//   }

// #ifdef NVS_LORAWAN_KEYS
//   ret = nvs_manager_read(NVS_LORAWAN_DEV_EUI_ID, dev_eui, sizeof(dev_eui));
//   if (ret >= 0) {
//     LOG_INF("Read Dev EUI from NVS");
//     // print_bytes("Dev EUI", dev_eui, sizeof(dev_eui));
//   } else {
//     LOG_WRN("Dev EUI not found or incomplete in NVS. Generating...");
//   }

//   ret = nvs_manager_read(NVS_LORAWAN_JOIN_EUI_ID, join_eui,
//   sizeof(join_eui)); if (ret >= 0) {
//     LOG_INF("Read Join EUI from NVS");
//     // print_bytes("Join EUI", join_eui, sizeof(join_eui));
//   } else {
//     LOG_WRN("Join EUI not found or incomplete in NVS, Generating...");
//   }

//   ret = nvs_manager_read(NVS_LORAWAN_APP_KEY_ID, app_key, sizeof(app_key));
//   if (ret >= 0) {
//     LOG_INF("Read App Key from NVS");
//     // print_bytes("App Key", app_key, sizeof(app_key));
//   } else {
//     LOG_WRN("App Key not found or incomplete in NVS, Generating...");
//   }
// #endif

//   join_cfg.mode = LORAWAN_ACT_OTAA;
//   join_cfg.dev_eui = dev_eui;
//   join_cfg.otaa.join_eui = join_eui;
//   join_cfg.otaa.app_key = app_key;
//   join_cfg.otaa.nwk_key = app_key;
//   join_cfg.otaa.dev_nonce = dev_nonce;

//   LOG_INF("Dev Nonce: %d", dev_nonce);

//   int i = 0;
//   do {
//     LOG_INF("Joining network using OTAA, devNonce: %d; attempt: %d",
//             join_cfg.otaa.dev_nonce, i++);
//     int join_ret = lorawan_join(&join_cfg);
//     if (join_ret < 0) {
//       if (join_ret == -ETIMEDOUT) {
//         LOG_WRN("Timed-out waiting for response.");
//       } else {
//         LOG_ERR("Join failed (%d)", join_ret);
//       }
//     } else {
//       LOG_INF("Join successful.");
//     }
//     dev_nonce++;
//     join_cfg.otaa.dev_nonce = dev_nonce;

//     // Save updated dev_nonce to NVS using nvs_manager_write
//     int write_ret =
//         nvs_manager_write(NVS_DEVNONCE_ID, &dev_nonce, sizeof(dev_nonce));
//     if (write_ret < 0) {
//       LOG_ERR("NVS: Failed to write id %d (%d)", NVS_DEVNONCE_ID, ret);
//       k_sleep(K_MSEC(5000));
//     }
//     ret = join_ret;

//   } while (ret != 0);

//   LOG_INF("Sending data...");
//   while (1) {
//     ret = lorawan_send(5, data, sizeof(data), LORAWAN_MSG_CONFIRMED);

//     /*
//      * Note: The stack may return -EAGAIN if the provided data
//      * length exceeds the maximum possible one for the region and
//      * datarate. But since we are just sending the same data here,
//      * we'll just continue.
//      */
//     if (ret == -EAGAIN) {
//       LOG_ERR("lorawan_send failed: %d. Continuing...", ret);
//       k_sleep(DELAY);
//       continue;
//     }

//     if (ret < 0) {
//       LOG_ERR("lorawan_send failed: %d", ret);
//       return 0;
//     }

//     LOG_INF("Data sent!");
//     k_sleep(DELAY);
//   }
// }
#include "heartbeat_work.h"
#include "led_manager.h"
#include "lora_app.h"
#include "lora_manager.h"
#include "nfc_manager.h"
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

// External font declarations
LV_FONT_DECLARE(roboto_28);
LV_FONT_DECLARE(roboto_36);
LV_FONT_DECLARE(roboto_bold_42);

int main(void) {
  LOG_INF("FeedbackNow System Starting...");

  // Initialize all subsystems
  int ret = system_init();
  if (ret != 0) {
    LOG_ERR("System initialization failed: %d", ret);
    return ret;
  }

  // Initialize new architecture components
  ret = state_manager_init();
  if (ret != 0) {
    LOG_ERR("State manager initialization failed: %d", ret);
    return ret;
  }

  ret = led_manager_init();
  if (ret != 0) {
    LOG_ERR("LED manager initialization failed: %d", ret);
    return ret;
  }

  ret = lora_manager_init();
  if (ret != 0) {
    LOG_ERR("LoRa manager initialization failed: %d", ret);
    return ret;
  }

  ret = system_monitor_init();
  if (ret != 0) {
    LOG_ERR("System monitor initialization failed: %d", ret);
    return ret;
  }

  // Initialize NFC manager (now done here, not delayed)
  // ret = nfc_manager_init();
  // if (ret != 0) {
  //   LOG_ERR("NFC manager initialization failed: %d", ret);
  //   return ret;
  // }

  // Initialize heartbeat
  heartbeat_init();

  // Start the state manager thread
  LOG_INF("Starting state manager thread...");
  k_thread_start(state_manager_thread_id);
  LOG_INF("State manager thread start command issued");

  // Start the LoRa thread (after LoRaWAN stack is initialized)
  // Small delay to ensure LoRaWAN stack is fully ready
  k_sleep(K_MSEC(100));
  LOG_INF("Starting LoRa thread...");
  k_thread_start(lora_thread_id);
  LOG_INF("LoRa thread start command issued");

  // Start NFC manager thread
  // k_thread_start(nfc_manager_thread_id);
  // LOG_INF("NFC manager thread start command issued");

  // LOG_INF("LoRa thread started after LoRaWAN stack initialization");

  // ========================================================================
  // LVGL DISPLAY DEMO - Simple initialization
  // ========================================================================
  LOG_INF("=== INITIALIZING LVGL DISPLAY ===");

  const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display_dev)) {
    LOG_ERR("Display device not ready");
    return -ENODEV;
  }
  LOG_INF("Display device ready: %s", display_dev->name);

  // Display info
  struct display_capabilities caps;
  display_get_capabilities(display_dev, &caps);
  LOG_INF("Display: %dx%d, format=%d", caps.x_resolution, caps.y_resolution,
          caps.current_pixel_format);

  // Get the active screen (don't create a new one)
  lv_obj_t *scr = lv_scr_act();

  // IMPORTANT: For monochrome EPD, set background to WHITE (1)
  lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  // Heading - Using roboto_28 (closest to 24 we have)
  lv_obj_t *heading = lv_label_create(scr);
  lv_label_set_text(heading, "last cleaned at:");
  lv_obj_set_style_text_font(heading, &roboto_bold_42,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(heading, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 20);
  // Add a horizontal line below the heading using LVGL
  lv_obj_t *line = lv_line_create(scr);
  static lv_point_t line_points[] = {{-80, 0}, {80, 0}}; // 160px wide centered
  lv_line_set_points(line, line_points, 2);
  lv_obj_set_style_line_width(line, 2,
                              LV_PART_MAIN); // adjust thickness as needed
  lv_obj_set_style_line_color(line, lv_color_black(), LV_PART_MAIN);
  lv_obj_align_to(line, heading, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // Line 1 - roboto_36
  lv_obj_t *line1 = lv_label_create(scr);
  lv_label_set_text(line1, "2025-10-12 13:25:65");
  lv_obj_set_style_text_font(line1, &roboto_36,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(line1, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(line1, LV_ALIGN_CENTER, 0, -30);

  // Line 2 - roboto_36
  lv_obj_t *line2 = lv_label_create(scr);
  lv_label_set_text(line2, "2025-10-12 18:35:47");
  lv_obj_set_style_text_font(line2, &roboto_36,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(line2, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(line2, LV_ALIGN_CENTER, 0, 30);

  // Line 3 - roboto_36
  lv_obj_t *line3 = lv_label_create(scr);
  lv_label_set_text(line3, "2025-10-12 19:25:37");
  lv_obj_set_style_text_font(line3, &roboto_36,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(line3, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(line3, LV_ALIGN_CENTER, 0, 90);

  LOG_INF("LVGL widgets created");
  LOG_INF("=== LVGL DISPLAY DEMO COMPLETE ===");
  // ========================================================================

  // Send system ready event
  system_event_msg_t ready_event = {.event_type = EVENT_SYSTEM_READY};
  state_manager_send_event(&ready_event);

  LOG_INF("System initialization complete. All threads started.");
  LOG_INF("System ready for user input.");

  // Log thread status
  LOG_INF("=== THREAD STATUS CHECK ===");
  LOG_INF("Main thread ID: %p", k_current_get());
  LOG_INF("State manager thread ID: %p", state_manager_thread_id);
  LOG_INF("LoRa thread ID: %p", lora_thread_id);
  LOG_INF("NFC manager thread ID: %p", nfc_manager_thread_id);

  // Thread status logged above

  // Main loop - monitor system health and update LVGL
  int loop_count = 0;
  while (1) {
    // Update LVGL periodically
    lv_task_handler();

    // Small sleep to prevent tight loop
    k_sleep(K_MSEC(100));

    // Periodic thread health check
    if (loop_count % 100 == 0) { // Every ~10 seconds
      LOG_INF("Uptime: %llu ms", k_uptime_get());
    }
    loop_count++;
  }
}
