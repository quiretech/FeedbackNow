#define ENABLE_NFC

#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "buttons.h"
#include "leds.h"
#include "power_ctrl.h"
#ifdef ENABLE_NFC
#include "pn5180.h"
#endif

/* Custom fonts */
LV_FONT_DECLARE(roboto_20);
LV_FONT_DECLARE(roboto_semibold_25);
LV_FONT_DECLARE(roboto_28);
LV_FONT_DECLARE(roboto_bold_42);

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#ifdef ENABLE_NFC
/* Thread stack sizes */
#define NFC_THREAD_STACK_SIZE 2048

/* Message queue sizes */
#define NFC_QUEUE_SIZE 10
#endif

/* UI Configuration */
#define UI_NUM_BUTTONS 6
#define UI_CIRCLE_SIZE 32
#define UI_CIRCLE_SPACING 20
#define UI_CIRCLE_Y 70
#define UI_INACTIVITY_TIMEOUT_MS (0.5 * 60 * 1000) // 3 minutes

/* System states */
enum system_state { SYSTEM_IDLE, SYSTEM_NFC_SCANNING };

#define STATUS_LED_ID 0

#ifdef ENABLE_NFC
/* Message structures */
struct nfc_event {
  enum { NFC_EVENT_TAG_DETECTED, NFC_EVENT_NO_TAG, NFC_EVENT_ERROR } type;
  uint8_t uid[8];
  uint32_t timestamp;
};

/* Device tree definitions */
#define PN5180_DEVICE DT_NODELABEL(pn5180)
#endif
#define DISPLAY_NODE DT_CHOSEN(zephyr_display)

/* Global variables */
#ifdef ENABLE_NFC
static const struct device *pn5180_dev = DEVICE_DT_GET(PN5180_DEVICE);
#endif
static const struct device *display_dev = DEVICE_DT_GET(DISPLAY_NODE);
#ifdef ENABLE_NFC
static uint8_t last_nfc_uid[8] = {0};
static bool nfc_tag_detected = false;
static uint32_t nfc_scan_count = 0;
static int64_t last_scan_time = 0;
#endif
static uint32_t button_press_count = 0;
static uint32_t button_press_counts[UI_NUM_BUTTONS] = {
    0}; // Per-button press counters
static bool button_states[NUM_BUTTONS] = {false};
static int64_t system_start_time;
static int current_led_id = -1;

/* LVGL UI objects */
static lv_obj_t *button_circles[UI_NUM_BUTTONS];
static lv_obj_t *nfc_container;
static lv_obj_t *nfc_uid_label;
static lv_obj_t *nfc_time_label;

/* Splash screen */
static lv_obj_t *splash_screen;
static lv_obj_t *demo_container;
static bool is_splash_active = true;
static int64_t last_activity_time = 0;

/* Threading and synchronization */
#ifdef ENABLE_NFC
K_MSGQ_DEFINE(nfc_queue, sizeof(struct nfc_event), NFC_QUEUE_SIZE, 4);
#endif
K_MUTEX_DEFINE(
    spi_mutex); // Protects SPI bus - will be used for EPD when LVGL is added
#ifdef ENABLE_NFC
K_SEM_DEFINE(nfc_scan_sem, 0, 1);
#endif

/* LED blink timer */
static void led_off_callback(struct k_timer *timer);
static K_TIMER_DEFINE(led_off_timer, led_off_callback, NULL);

/* System state management */
static enum system_state current_state = SYSTEM_IDLE;
static K_MUTEX_DEFINE(state_mutex);

#ifdef ENABLE_NFC
/* Thread data structures */
static struct k_thread nfc_thread;
K_THREAD_STACK_DEFINE(nfc_thread_stack, NFC_THREAD_STACK_SIZE);
#endif

/* Function prototypes */
static void handle_button_event(button_event_t *event);
#ifdef ENABLE_NFC
static void nfc_thread_entry(void *arg1, void *arg2, void *arg3);
#endif
static void set_system_state(enum system_state new_state);
static void ui_create_button_circles(void);
static void ui_update_button_circle(int button_id, bool filled);
static void ui_create_nfc_status(void);
static void ui_update_nfc_status(void);
static void ui_create_splash_screen(void);
static void ui_show_demo(void);
static void ui_show_splash(void);

int main(void) {
  int ret;

  LOG_INF("Starting NFC + Button + LED + LVGL Demo");

  system_start_time = k_uptime_get();

  // Initialize buttons
  ret = buttons_init();
  if (ret < 0) {
    LOG_ERR("Button initialization failed: %d", ret);
    return ret;
  }

  // Initialize LEDs
  ret = leds_init();
  if (ret < 0) {
    LOG_ERR("LED initialization failed: %d", ret);
    return ret;
  }

  // Initialize power enable GPIOs and enable rails by default
  ret = power_ctrl_init();
  if (ret < 0) {
    LOG_ERR("Power control init failed: %d", ret);
    return ret;
  }

  power_ctrl_set(POWER_EN_3V3, true);
  power_ctrl_set(POWER_EN_1V8, true);
  power_ctrl_set(POWER_EN_3V3A, true);
  power_ctrl_set(POWER_EN_3V6, true);

  // Initialize Display
  if (!device_is_ready(display_dev)) {
    LOG_ERR("Display device not ready");
    return -ENODEV;
  }
  LOG_INF("Display device ready");

  // Set up screen with white background
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  // Create demo container (holds all demo UI, hidden initially)
  demo_container = lv_obj_create(scr);
  lv_obj_set_size(demo_container, 400, 300);
  lv_obj_set_pos(demo_container, 0, 0);
  lv_obj_set_style_bg_opa(demo_container, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(demo_container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(demo_container, 0, LV_PART_MAIN);
  lv_obj_clear_flag(demo_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(demo_container, LV_OBJ_FLAG_HIDDEN); // Hidden initially

  // Create button circles UI (inside demo container)
  ui_create_button_circles();

  // Create horizontal divider line (inside demo container)
  static lv_point_precise_t line_points[] = {{20, 0}, {380, 0}};
  lv_obj_t *divider = lv_line_create(demo_container);
  lv_line_set_points(divider, line_points, 2);
  lv_obj_set_style_line_color(divider, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_line_width(divider, 2, LV_PART_MAIN);
  lv_obj_set_pos(divider, 0, 115);

  // Create NFC status box (inside demo container)
  ui_create_nfc_status();

  // Create splash screen (visible initially)
  ui_create_splash_screen();

  // Force initial display refresh
  lv_task_handler();
  LOG_INF("LVGL UI initialized - splash screen active");

#ifdef ENABLE_NFC
  // Initialize NFC
  if (!device_is_ready(pn5180_dev)) {
    LOG_ERR("PN5180 device not ready");
    return -ENODEV;
  }

  ret = pn5180_init(pn5180_dev);
  if (ret < 0) {
    LOG_ERR("PN5180 init failed: %d", ret);
    return ret;
  }

  ret = pn5180_configure(pn5180_dev, PN5180_PROTOCOL_ISO15693);
  if (ret < 0) {
    LOG_ERR("PN5180 configure failed: %d", ret);
    return ret;
  }

  LOG_INF("PN5180 NFC initialized successfully");

  // Create NFC scanning thread
  k_thread_create(&nfc_thread, nfc_thread_stack,
                  K_THREAD_STACK_SIZEOF(nfc_thread_stack), nfc_thread_entry,
                  NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
  k_thread_name_set(&nfc_thread, "nfc_scanner");

  LOG_INF("System ready - NFC, Buttons, LEDs, Display active");
#else
  LOG_INF("System ready - Buttons, LEDs, Display active (NFC disabled)");
#endif

  // Main loop - handles button events and LVGL
  while (1) {
    button_event_t event;

    if (buttons_get_event(&event, K_MSEC(10))) {
      handle_button_event(&event);
    }

    // Only process LVGL when NFC is not using SPI bus
    if (current_state != SYSTEM_NFC_SCANNING) {
      lv_task_handler();
    }

    // Check for inactivity timeout - return to splash screen
    if (!is_splash_active && last_activity_time > 0) {
      int64_t idle_time = k_uptime_get() - last_activity_time;
      if (idle_time > UI_INACTIVITY_TIMEOUT_MS) {
        ui_show_splash();
      }
    }

    k_msleep(10);
  }

  return 0;
}

/* Handle button events */
static void handle_button_event(button_event_t *event) {
  if (event->type == BUTTON_EVENT_PRESS) {
    // IMMEDIATE: Turn on LED first for responsive feedback
    current_led_id = STATUS_LED_ID;
    led_set(STATUS_LED_ID, true);
    k_timer_start(&led_off_timer, K_MSEC(500), K_NO_WAIT);

    // If splash screen is active, switch to demo and return
    if (is_splash_active) {
      ui_show_demo();
      return;
    }

    // Update activity time for inactivity timeout
    last_activity_time = k_uptime_get();

    button_press_count++;

    // Increment per-button counter for buttons 0-5
    if (event->button_id < UI_NUM_BUTTONS) {
      button_press_counts[event->button_id]++;
    }

    // Toggle button state on press
    button_states[event->button_id] = !button_states[event->button_id];

    LOG_INF("\nButton %d pressed (total: %d, state: %s)", event->button_id,
            button_press_count, button_states[event->button_id] ? "ON" : "OFF");

    // Special handling for button 0 (NFC scan trigger)
    if (event->button_id == 0) {
#ifdef ENABLE_NFC
      LOG_INF("Button 0 pressed - triggering NFC scan");
      if (device_is_ready(pn5180_dev)) {
        k_sem_give(&nfc_scan_sem);
      }
#endif
    }

    // Batch UI updates into single refresh (this can be slow on e-paper)
    lv_disp_t *disp = lv_disp_get_default();
    lv_disp_enable_invalidation(disp, false);

    // Update circle on display for buttons 0-5
    if (event->button_id < UI_NUM_BUTTONS) {
      ui_update_button_circle(event->button_id,
                              button_states[event->button_id]);
    }

    // Refresh NFC status (updates elapsed time)
    ui_update_nfc_status();

    // Re-enable and force single refresh
    lv_disp_enable_invalidation(disp, true);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(disp);

  } else if (event->type == BUTTON_EVENT_RELEASE) {
    LOG_INF("Button %d released", event->button_id);
  }
}

#ifdef ENABLE_NFC
/* NFC scanning thread */
static void nfc_thread_entry(void *arg1, void *arg2, void *arg3) {
  uint8_t uid[8] = {0};
  int ret;

  LOG_INF("NFC thread started");

  while (1) {
    // Wait for NFC scan trigger
    k_sem_take(&nfc_scan_sem, K_FOREVER);

    LOG_INF("NFC scan triggered");
    set_system_state(SYSTEM_NFC_SCANNING);
    nfc_scan_count++;
    last_scan_time = k_uptime_get();

    // Lock SPI bus for NFC operation
    k_mutex_lock(&spi_mutex, K_FOREVER);
    ret = pn5180_get_inventory(pn5180_dev, uid, sizeof(uid));
    k_mutex_unlock(&spi_mutex);

    // Process results
    if (ret == 0) {
      LOG_INF("*** TAG DETECTED ***");
      LOG_INF("UID: %02X %02X %02X %02X %02X %02X %02X %02X", uid[0], uid[1],
              uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);

      memcpy(last_nfc_uid, uid, sizeof(uid));
      nfc_tag_detected = true;

    } else if (ret == PN5180_ERR_TIMEOUT) {
      LOG_INF("No NFC tag detected (timeout)");
      nfc_tag_detected = false;
      memset(last_nfc_uid, 0, sizeof(last_nfc_uid));

    } else {
      LOG_ERR("NFC scan failed with error: %d", ret);
    }

    // Update NFC status on display
    ui_update_nfc_status();

    set_system_state(SYSTEM_IDLE);
    LOG_INF("NFC scan complete");
  }
}
#endif

/* Set system state with logging */
static void set_system_state(enum system_state new_state) {
  k_mutex_lock(&state_mutex, K_FOREVER);

  const char *state_names[] = {"IDLE", "NFC_SCANNING"};

  if (current_state != new_state) {
    LOG_INF("System state: %s -> %s", state_names[current_state],
            state_names[new_state]);
    current_state = new_state;
  }

  k_mutex_unlock(&state_mutex);
}

/* LED off callback */
static void led_off_callback(struct k_timer *timer) {
  if (current_led_id >= 0 && current_led_id < NUM_LEDS) {
    led_set(current_led_id, false);
    LOG_INF("LED %d turned off after blink", current_led_id);
    current_led_id = -1;
  }
}

/* Create button circle indicators */
static void ui_create_button_circles(void) {
  // Calculate starting X position to center the circles
  // Total width = (circles * size) + (gaps * spacing)
  int total_width = (UI_NUM_BUTTONS * UI_CIRCLE_SIZE) +
                    ((UI_NUM_BUTTONS - 1) * UI_CIRCLE_SPACING);
  int start_x = (400 - total_width) / 2; // Display is 400px wide

  for (int i = 0; i < UI_NUM_BUTTONS; i++) {
    // Create circle object
    button_circles[i] = lv_obj_create(demo_container);
    lv_obj_set_size(button_circles[i], UI_CIRCLE_SIZE, UI_CIRCLE_SIZE);

    // Position the circle
    int x = start_x + (i * (UI_CIRCLE_SIZE + UI_CIRCLE_SPACING));
    lv_obj_set_pos(button_circles[i], x, UI_CIRCLE_Y);

    // Style: circular shape with black border, white fill (unfilled state)
    lv_obj_set_style_radius(button_circles[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button_circles[i], lv_color_white(),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button_circles[i], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button_circles[i], lv_color_black(),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(button_circles[i], 2, LV_PART_MAIN);

    // Remove default padding and scrollbar
    lv_obj_set_style_pad_all(button_circles[i], 0, LV_PART_MAIN);
    lv_obj_clear_flag(button_circles[i], LV_OBJ_FLAG_SCROLLABLE);
  }

  LOG_INF("Created %d button circles", UI_NUM_BUTTONS);
}

/* Update button circle fill state */
static void ui_update_button_circle(int button_id, bool filled) {
  if (button_id < 0 || button_id >= UI_NUM_BUTTONS) {
    return;
  }

  if (filled) {
    // Filled state: black background
    lv_obj_set_style_bg_color(button_circles[button_id], lv_color_black(),
                              LV_PART_MAIN);
  } else {
    // Unfilled state: white background
    lv_obj_set_style_bg_color(button_circles[button_id], lv_color_white(),
                              LV_PART_MAIN);
  }

  LOG_INF("Circle %d updated: %s", button_id, filled ? "filled" : "unfilled");
}

/* Create NFC status display box */
static void ui_create_nfc_status(void) {
  // Create container with black background
  nfc_container = lv_obj_create(demo_container);
  lv_obj_set_size(nfc_container, 380, 90);
  lv_obj_align(nfc_container, LV_ALIGN_CENTER, 0, 55);

  // Style: black background, no border
  lv_obj_set_style_bg_color(nfc_container, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(nfc_container, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(nfc_container, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(nfc_container, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(nfc_container, 10, LV_PART_MAIN);
  lv_obj_clear_flag(nfc_container, LV_OBJ_FLAG_SCROLLABLE);

  // UID label - Roboto 28, center-middle initially (moves to top after scan)
  nfc_uid_label = lv_label_create(nfc_container);
#ifdef ENABLE_NFC
  lv_label_set_text(nfc_uid_label, "Press B0 to scan");
  lv_obj_align(nfc_uid_label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_font(nfc_uid_label, &roboto_28, LV_PART_MAIN);
#else
  lv_label_set_text(nfc_uid_label, ""); // Hidden when NFC disabled
#endif
  lv_obj_set_style_text_color(nfc_uid_label, lv_color_white(), LV_PART_MAIN);

  // Time since scan label / Button counts label
  nfc_time_label = lv_label_create(nfc_container);
#ifdef ENABLE_NFC
  lv_label_set_text(nfc_time_label, "");
  lv_obj_align(nfc_time_label, LV_ALIGN_TOP_MID, 0, 35);
  lv_obj_set_style_text_font(nfc_time_label, &roboto_28, LV_PART_MAIN);
#else
  lv_label_set_text(nfc_time_label, "B0:0  B1:0  B2:0  B3:0  B4:0  B5:0");
  lv_obj_align(nfc_time_label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_font(nfc_time_label, &roboto_semibold_25, LV_PART_MAIN);
#endif
  lv_obj_set_style_text_color(nfc_time_label, lv_color_white(), LV_PART_MAIN);

  LOG_INF("NFC status box created");
}

/* Update NFC status display with current data */
static void ui_update_nfc_status(void) {
  static char uid_buf[48];
  static char time_buf[64];

#ifdef ENABLE_NFC
  // Update UID label
  if (nfc_tag_detected) {
    snprintf(uid_buf, sizeof(uid_buf), "UID: %02X%02X%02X%02X%02X%02X%02X%02X",
             last_nfc_uid[0], last_nfc_uid[1], last_nfc_uid[2], last_nfc_uid[3],
             last_nfc_uid[4], last_nfc_uid[5], last_nfc_uid[6],
             last_nfc_uid[7]);
    // Move to top after scan
    lv_obj_align(nfc_uid_label, LV_ALIGN_TOP_MID, 0, 0);
  } else if (nfc_scan_count > 0) {
    // Scan happened but no tag found
    snprintf(uid_buf, sizeof(uid_buf), "No tag found");
    // Move to top after scan
    lv_obj_align(nfc_uid_label, LV_ALIGN_TOP_MID, 0, 0);
  } else {
    // Never scanned yet - keep centered
    snprintf(uid_buf, sizeof(uid_buf), "Press B0 to scan");
    lv_obj_align(nfc_uid_label, LV_ALIGN_CENTER, 0, 0);
  }
  lv_label_set_text(nfc_uid_label, uid_buf);

  // Update time label
  if (last_scan_time > 0) {
    int64_t elapsed_ms = k_uptime_get() - last_scan_time;
    int seconds = (int)(elapsed_ms / 1000);

    if (seconds < 60) {
      snprintf(time_buf, sizeof(time_buf), "Last Scanned: %ds ago", seconds);
    } else {
      int minutes = seconds / 60;
      snprintf(time_buf, sizeof(time_buf), "Last Scanned: %dm %ds ago", minutes,
               seconds % 60);
    }
  } else {
    // No scan yet - leave time label empty
    snprintf(time_buf, sizeof(time_buf), "");
  }
  lv_label_set_text(nfc_time_label, time_buf);
#else
  // NFC disabled - show button press counters only
  snprintf(
      time_buf, sizeof(time_buf), "B0:%d  B1:%d  B2:%d  B3:%d  B4:%d  B5:%d",
      button_press_counts[0], button_press_counts[1], button_press_counts[2],
      button_press_counts[3], button_press_counts[4], button_press_counts[5]);
  lv_obj_align(nfc_time_label, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(nfc_time_label, time_buf);
#endif
}

/* Create splash screen */
static void ui_create_splash_screen(void) {
  splash_screen = lv_obj_create(lv_scr_act());
  lv_obj_set_size(splash_screen, 400, 300);
  lv_obj_set_pos(splash_screen, 0, 0);
  lv_obj_set_style_bg_color(splash_screen, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(splash_screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(splash_screen, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(splash_screen, 0, LV_PART_MAIN);
  lv_obj_clear_flag(splash_screen, LV_OBJ_FLAG_SCROLLABLE);

  // "FeedBack Now" - roboto_bold_42, center
  lv_obj_t *title = lv_label_create(splash_screen);
  lv_label_set_text(title, "FeedBackNow");
  lv_obj_set_style_text_font(title, &roboto_bold_42, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

  // "Demo" - roboto_20, below title
  lv_obj_t *subtitle = lv_label_create(splash_screen);
  lv_label_set_text(subtitle, "Demo");
  lv_obj_set_style_text_font(subtitle, &roboto_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(subtitle, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 10);

  // Horizontal line below subtitle
  static lv_point_precise_t splash_line[] = {{40, 0}, {360, 0}};
  lv_obj_t *line = lv_line_create(splash_screen);
  lv_line_set_points(line, splash_line, 2);
  lv_obj_set_style_line_color(line, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
  lv_obj_set_pos(line, 0, 180);

  // Footer - "QuireTech LLC | v1.1"
  lv_obj_t *footer = lv_label_create(splash_screen);
  lv_label_set_text(footer, "QuireTech LLC | v1.1");
  lv_obj_set_style_text_font(footer, &roboto_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(footer, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -20);

  LOG_INF("Splash screen created");
}

/* Switch from splash to demo UI */
static void ui_show_demo(void) {
  if (!is_splash_active) {
    return;
  }

  // Batch all UI changes into single refresh
  lv_disp_t *disp = lv_disp_get_default();
  lv_disp_enable_invalidation(disp, false);

  // Hide splash, show demo
  lv_obj_add_flag(splash_screen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(demo_container, LV_OBJ_FLAG_HIDDEN);
  is_splash_active = false;
  last_activity_time = k_uptime_get();

  // Re-enable and force single refresh
  lv_disp_enable_invalidation(disp, true);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(disp);

  LOG_INF("Switched to demo UI");
}

/* Switch from demo back to splash screen */
static void ui_show_splash(void) {
  if (is_splash_active) {
    return;
  }

  // Batch all UI changes into single refresh
  lv_disp_t *disp = lv_disp_get_default();
  lv_disp_enable_invalidation(disp, false);

  // Hide demo, show splash
  lv_obj_add_flag(demo_container, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(splash_screen, LV_OBJ_FLAG_HIDDEN);
  is_splash_active = true;
  last_activity_time = 0;

  // Re-enable and force single refresh
  lv_disp_enable_invalidation(disp, true);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(disp);

  LOG_INF("Returned to splash screen after inactivity");
}
