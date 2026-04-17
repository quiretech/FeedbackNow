/**
 * Display manager: EPD screen jobs and work queue. When EPD_ENABLED=0, no-ops.
 */
#include "display_manager.h"
#include "button_counter_store.h"
#include "eui_keys.h"
#include "last_cleaned_store.h"
#include "rail_manager.h"
#include "rtc.h"
#include "sys_config.h"
#include "tz_offset_store.h"

#include <stdio.h>
#include <time.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#if EPD_ENABLED
#include "ssd1683.h"
#include <lvgl.h>

/* Font declarations */
LV_FONT_DECLARE(roboto_20);
LV_FONT_DECLARE(roboto_28);
LV_FONT_DECLARE(roboto_32);
LV_FONT_DECLARE(roboto_36);
LV_FONT_DECLARE(roboto_bold_42);
/* Boot logo image (from assets/logo/bootLogo.c) */
LV_IMG_DECLARE(bootLogo);
/* Thanks/ack screen image (from assets/logo/AckEng.c) */
LV_IMG_DECLARE(ackEng);
/* Cleaning in progress image (from assets/display_screens/CleaningScreen.c) */
LV_IMG_DECLARE(cleaningScreen);
#endif

LOG_MODULE_REGISTER(display_mgr, CONFIG_LOG_DEFAULT_LEVEL);

#if EPD_ENABLED

/* Use sys_config.h for DISPLAY_JOB_QUEUE_SIZE, DISPLAY_JOB_ALIGN */

enum display_job_type {
  JOB_SHOW_LOGO,
  JOB_SHOW_LAST_CLEANED,
  JOB_SHOW_THANKS,
  JOB_SHOW_CLEANING,
  JOB_SHOW_CONNECTING,
  JOB_SHOW_DEVICE_INFO,
  JOB_FULL_REFRESH,
};

struct display_job {
  uint8_t type;
  uint8_t pad[3];
  uint32_t epoch; /* for LAST_CLEANED: use this if non-zero (pending from
                     downlink) */
};

K_MSGQ_DEFINE(display_jobq, sizeof(struct display_job), DISPLAY_JOB_QUEUE_SIZE,
              DISPLAY_JOB_ALIGN);

static struct k_work_delayable display_work;
/* Pending epoch from downlink; mutex protects set vs consume (SMF vs work
 * queue) */
static uint32_t pending_last_cleaned_epoch; /* 0 = none */
static K_MUTEX_DEFINE(pending_epoch_mutex);
/* Timer vs work queue: use atomic for thread safety */
static atomic_t cleaning_timer_active_atomic = ATOMIC_INIT(0);
static atomic_t cleaning_timer_expired_atomic = ATOMIC_INIT(0);
static enum display_screen_id current_screen = DISPLAY_SCREEN_LOGO;

/* Public vote: block new votes from THANKS (queued) until LAST_CLEANED render
 * after thanks_timer (see display_show_thanks_sync). */
static atomic_t vote_ui_busy = ATOMIC_INIT(0);
static atomic_t vote_ack_pending = ATOMIC_INIT(0);

/* LVGL display and screen objects */
static const struct device *lvgl_display_dev;
static lv_display_t *lvgl_display;
static lv_obj_t *screen_logo;
static lv_obj_t *screen_last_cleaned;
static lv_obj_t *screen_thanks;
static lv_obj_t *screen_cleaning;
static lv_obj_t *screen_connecting;
static lv_obj_t *screen_device_info;
static lv_obj_t *last_cleaned_label; /* Label on screen_last_cleaned */
/* Device Info screen labels */
static lv_obj_t *dev_info_heading;
static lv_obj_t *dev_info_deveui;
static lv_obj_t *dev_info_fw;
static lv_obj_t *dev_info_counters;

/* LVGL draw buffers (monochrome: +8 bytes for palette)
 * For DIRECT mode: need full screen buffer
 * Stride = (400+7)/8 = 50 bytes per row
 * Full buffer = 50 * 300 + 8 = 15008 bytes
 * Align buffers to avoid bus faults - use 4-byte alignment
 */
#define LVGL_STRIDE_BYTES ((400 + 7) / 8) /* 50 bytes per row */
#define LVGL_BUF_SIZE                                                          \
  ((LVGL_STRIDE_BYTES * 300) + 8) /* Full screen + palette */
static uint8_t __aligned(4) lvgl_buf1[LVGL_BUF_SIZE];
static uint8_t __aligned(4) lvgl_buf2[LVGL_BUF_SIZE];

static void enqueue_job(enum display_job_type type, uint32_t epoch);

/**
 * Force LVGL to render and flush. In DIRECT render mode, a single
 * lv_task_handler() call may not flush (LVGL can defer rendering to the next
 * tick). lv_refr_now() forces an immediate render pass, then lv_task_handler()
 * processes the flush. Belt-and-suspenders: if still not flushed, try once
 * more.
 */
static void force_lvgl_flush(void) {
  lv_refr_now(lvgl_display);
  lv_task_handler();
}

static void thanks_timer_expiry(struct k_timer *timer) {
  ARG_UNUSED(timer);
  /* Timer context — only enqueue, never call blocking APIs. */
  if (atomic_get(&cleaning_timer_active_atomic)) {
    enqueue_job(JOB_SHOW_CLEANING, 0);
    return;
  }
  enqueue_job(JOB_SHOW_LAST_CLEANED, 0);
}

static void cleaning_timer_expiry(struct k_timer *timer) {
  ARG_UNUSED(timer);
  /* Timer context — set flag and enqueue. RTC/EEPROM work deferred to handler.
   */
  atomic_set(&cleaning_timer_active_atomic, 0);
  atomic_set(&cleaning_timer_expired_atomic, 1);
  enqueue_job(JOB_SHOW_LAST_CLEANED, 0);
}

K_TIMER_DEFINE(thanks_timer, thanks_timer_expiry, NULL);
K_TIMER_DEFINE(cleaning_timer, cleaning_timer_expiry, NULL);

/* For display_show_thanks_sync: caller blocks until THANKS render completes. */
K_SEM_DEFINE(thanks_done_sem, 0, 1);
static atomic_t thanks_sync_waiting = ATOMIC_INIT(0);

/* For display_show_logo_sync: boot logo before LoRa thread (shared SPI). */
K_SEM_DEFINE(logo_done_sem, 0, 1);
static atomic_t logo_sync_waiting = ATOMIC_INIT(0);

/* For display_show_last_cleaned_sync: NFC check-out / immediate last cleaned. */
K_SEM_DEFINE(last_cleaned_done_sem, 0, 1);
static atomic_t last_cleaned_sync_waiting = ATOMIC_INIT(0);

/* For display_show_cleaning_sync: NFC check-in / immediate cleaning screen. */
K_SEM_DEFINE(cleaning_done_sem, 0, 1);
static atomic_t cleaning_sync_waiting = ATOMIC_INIT(0);

/* 1 while display_work_handler holds SPI for EPD (LoRa shares arduino_spi). */
static atomic_t display_epd_spi_busy = ATOMIC_INIT(0);

/* LVGL display flush callback - writes LVGL framebuffer to display */
static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area,
                          uint8_t *px_map) {
  const struct device *dev = lv_display_get_user_data(display);
  struct display_buffer_descriptor desc;

  /* 1. CLAMP COORDINATES: Prevent the 41431-pixel bug */
  int32_t x1 = MAX(area->x1, 0);
  int32_t y1 = MAX(area->y1, 0);
  int32_t x2 = MIN(area->x2, 399); // SSD1683 Max Width - 1
  int32_t y2 = MIN(area->y2, 299); // SSD1683 Max Height - 1

  uint16_t width = x2 - x1 + 1;
  uint16_t height = y2 - y1 + 1;

  /* 2. Skip palette (Required for LV_COLOR_FORMAT_I1) */
  px_map += 8;

  /* 3. Calculate pitch based on the full stride you defined in init */
  uint32_t pitch_bytes = (400 + 7) / 8; // Should be 50 bytes

  desc.buf_size = pitch_bytes * height;
  desc.width = width;
  desc.height = height;
  desc.pitch = pitch_bytes;

  /* 4. Write to driver using clamped coordinates */
  int ret = display_write(dev, x1, y1, &desc, px_map);
  if (ret < 0) {
    LOG_ERR("[EPD] display_write error: %d", ret);
  }

  lv_display_flush_ready(display);
}

/* Create LVGL screens for each display state */
static void create_lvgl_screens(void) {
  lv_obj_t *label;

  /* Screen: LOGO – boot logo image centered */
  screen_logo = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_logo, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_logo, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_t *img_logo = lv_img_create(screen_logo);
  lv_img_set_src(img_logo, &bootLogo);
  lv_obj_center(img_logo);
  lv_obj_add_flag(screen_logo, LV_OBJ_FLAG_HIDDEN);

  /* Screen: LAST_CLEANED (400x300): top space, heading, gap, timestamp, bottom
   * space */
  screen_last_cleaned = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_last_cleaned, lv_color_white(),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_last_cleaned, LV_OPA_COVER, LV_PART_MAIN);

  /* Container for flex layout; must have white background so it doesn't draw
   * black */
  lv_obj_t *cont = lv_obj_create(screen_last_cleaned);
  lv_obj_set_size(cont, 400, 300);
  lv_obj_center(cont);
  lv_obj_set_style_bg_color(cont, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);

  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(cont, 40, LV_PART_MAIN);

  label = lv_label_create(cont);
  lv_label_set_text(label, "LAST CLEANED");
  lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);

  last_cleaned_label = lv_label_create(cont);
  lv_label_set_text(last_cleaned_label, "2026/01/01 00:00");
  lv_obj_set_style_text_font(last_cleaned_label, &roboto_36, LV_PART_MAIN);
  lv_obj_set_style_text_color(last_cleaned_label, lv_color_black(),
                              LV_PART_MAIN);

  lv_obj_add_flag(screen_last_cleaned, LV_OBJ_FLAG_HIDDEN);

  /* Screen: THANKS – ack image centered (same style as logo, from
   * assets/logo/AckEng.c) */
  screen_thanks = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_thanks, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_thanks, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_t *img_thanks = lv_img_create(screen_thanks);
  lv_img_set_src(img_thanks, &ackEng);
  lv_obj_center(img_thanks);
  lv_obj_add_flag(screen_thanks, LV_OBJ_FLAG_HIDDEN);

  /* Screen: CLEANING – bitmap centered (same style as logo/thanks) */
  screen_cleaning = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_cleaning, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_cleaning, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_t *img_cleaning = lv_img_create(screen_cleaning);
  lv_img_set_src(img_cleaning, &cleaningScreen);
  lv_obj_center(img_cleaning);
  lv_obj_add_flag(screen_cleaning, LV_OBJ_FLAG_HIDDEN);

  /* Screen: CONNECTING – text centered in middle of 400x300 */
  screen_connecting = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_connecting, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_connecting, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *cont_connecting = lv_obj_create(screen_connecting);
  lv_obj_set_size(cont_connecting, 400, 300);
  lv_obj_center(cont_connecting);
  lv_obj_set_style_bg_color(cont_connecting, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cont_connecting, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(cont_connecting, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(cont_connecting, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(cont_connecting, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont_connecting, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  label = lv_label_create(cont_connecting);
  lv_label_set_text(label, "Connecting...");
  lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);

  lv_obj_add_flag(screen_connecting, LV_OBJ_FLAG_HIDDEN);

  /* Screen: DEVICE_INFO – center aligned, flex, heading 32.c, info 28.c, order:
   * heading, eui, counters, fw */
  screen_device_info = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_device_info, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_device_info, LV_OPA_COVER, LV_PART_MAIN);

  // Flex container for device info
  lv_obj_t *cont_devinfo = lv_obj_create(screen_device_info);
  lv_obj_set_size(cont_devinfo, 400, 300);
  lv_obj_center(cont_devinfo);
  lv_obj_set_style_bg_color(cont_devinfo, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cont_devinfo, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(cont_devinfo, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(cont_devinfo, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(cont_devinfo, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont_devinfo, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(cont_devinfo, 14, LV_PART_MAIN);

  /* Heading */
  dev_info_heading = lv_label_create(cont_devinfo);
  lv_label_set_text(dev_info_heading, "FeedBackNow FlexBox");
  lv_obj_set_style_text_font(dev_info_heading, &roboto_32, LV_PART_MAIN);
  lv_obj_set_style_text_color(dev_info_heading, lv_color_black(), LV_PART_MAIN);

  /* DevEUI as second line */
  dev_info_deveui = lv_label_create(cont_devinfo);
  lv_label_set_text(dev_info_deveui, "DevEUI: 00:00:00:00:00:00:00:00");
  lv_obj_set_style_text_font(dev_info_deveui, &roboto_28, LV_PART_MAIN);
  lv_obj_set_style_text_color(dev_info_deveui, lv_color_black(), LV_PART_MAIN);

  /* Button counters next */
  dev_info_counters = lv_label_create(cont_devinfo);
  lv_label_set_text(dev_info_counters, "0x0:0 0x1:0 0x2:0 0x3:0 0x4:0 0x5:0");
  lv_obj_set_style_text_font(dev_info_counters, &roboto_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(dev_info_counters, lv_color_black(),
                              LV_PART_MAIN);

  /* FW version last */
  dev_info_fw = lv_label_create(cont_devinfo);
  lv_label_set_text(dev_info_fw, "Version: " FW_VERSION_STRING);
  lv_obj_set_style_text_font(dev_info_fw, &roboto_28, LV_PART_MAIN);
  lv_obj_set_style_text_color(dev_info_fw, lv_color_black(), LV_PART_MAIN);

  /* Footer: QuireTech LLC 2026, centered, roboto20 font */
  lv_obj_t *dev_info_footer = lv_label_create(cont_devinfo);
  lv_label_set_text(dev_info_footer, "QuireTech LLC 2026");
  lv_obj_set_style_text_font(dev_info_footer, &roboto_20, LV_PART_MAIN);
  lv_obj_set_style_text_color(dev_info_footer, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_align(dev_info_footer, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);

  lv_obj_add_flag(screen_device_info, LV_OBJ_FLAG_HIDDEN);
}

/* Format epoch as yyyy/mm/dd hh:mm (UTC). Buffer at least 17 bytes. */
static void format_epoch_yyyymmdd_hhmm(uint32_t epoch_s, char *buf,
                                       size_t buf_len) {
  if (buf == NULL || buf_len < 17) {
    if (buf && buf_len > 0) {
      buf[0] = '\0';
    }
    return;
  }
  time_t tt = (time_t)epoch_s;
  struct tm tm_utc = {0};
  if (gmtime_r(&tt, &tm_utc) == NULL) {
    buf[0] = '\0';
    return;
  }
  (void)snprintf(buf, buf_len, "%04d/%02d/%02d %02d:%02d",
                 tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                 tm_utc.tm_hour, tm_utc.tm_min);
}

static void do_render(const struct device *display, enum display_job_type type,
                      uint32_t epoch) {
  int ret;
  char ts[64]; /* enough for "%04d/%02d/%02d %02d:%02d" + locale; avoids
                  -Wformat-truncation */
  lv_obj_t *scr_to_show = NULL;

  if (display == NULL || !device_is_ready(display)) {
    LOG_ERR("[EPD] display not ready");
    return;
  }

  /* Power on EPD. Retry if SPI is busy (shared with LoRa radio). */
  for (int attempt = 0; attempt < 3; attempt++) {
    ret = display_blanking_off(display);
    if (ret == 0) {
      break;
    }
    LOG_WRN("[EPD] blanking_off failed: %d (attempt %d/3)", ret, attempt + 1);
    k_msleep(2000);
  }
  if (ret < 0) {
    LOG_ERR("[EPD] blanking_off failed after retries: %d", ret);
    return;
  }

  /* Hide all screens first */
  if (screen_logo) {
    lv_obj_add_flag(screen_logo, LV_OBJ_FLAG_HIDDEN);
  }
  if (screen_last_cleaned) {
    lv_obj_add_flag(screen_last_cleaned, LV_OBJ_FLAG_HIDDEN);
  }
  if (screen_thanks) {
    lv_obj_add_flag(screen_thanks, LV_OBJ_FLAG_HIDDEN);
  }
  if (screen_cleaning) {
    lv_obj_add_flag(screen_cleaning, LV_OBJ_FLAG_HIDDEN);
  }
  if (screen_connecting) {
    lv_obj_add_flag(screen_connecting, LV_OBJ_FLAG_HIDDEN);
  }
  if (screen_device_info) {
    lv_obj_add_flag(screen_device_info, LV_OBJ_FLAG_HIDDEN);
  }

  /* Show the requested screen */
  switch (type) {
  case JOB_SHOW_LOGO:
    LOG_INF("[EPD] show LOGO (FeedBackNow FlexBox)");
    scr_to_show = screen_logo;
    break;
  case JOB_SHOW_LAST_CLEANED: {
    /* Apply timezone offset for display only (all internals stay UTC). */
    int16_t tz_min = 0;
    (void)tz_offset_store_get(&tz_min);
    int64_t display_epoch = (int64_t)epoch + (int64_t)tz_min * 60;
    if (display_epoch < 0) {
      display_epoch = 0;
    }
    if (display_epoch > (int64_t)UINT32_MAX) {
      display_epoch = (int64_t)UINT32_MAX;
    }
    format_epoch_yyyymmdd_hhmm((uint32_t)display_epoch, ts, sizeof(ts));
    LOG_INF("[EPD] show LAST_CLEANED %s", ts);
    if (last_cleaned_label) {
      lv_label_set_text(last_cleaned_label, ts);
    }
    scr_to_show = screen_last_cleaned;
    break;
  }
  case JOB_SHOW_THANKS:
    LOG_INF("[EPD] show THANKS (5s then last cleaned)");
    scr_to_show = screen_thanks;
    break;
  case JOB_SHOW_CLEANING:
    LOG_INF("[EPD] show CLEANING (45min auto-revert)");
    scr_to_show = screen_cleaning;
    break;
  case JOB_SHOW_CONNECTING:
    LOG_INF("[EPD] show CONNECTING");
    scr_to_show = screen_connecting;
    break;
  case JOB_SHOW_DEVICE_INFO: {
    LOG_INF("[EPD] show DEVICE_INFO");
    /* Format DevEUI */
    if (dev_info_deveui) {
      static const uint8_t dev_eui[] = LORAWAN_DEV_EUI;
      char deveui_str[32];
      (void)snprintf(deveui_str, sizeof(deveui_str),
                     "EUI: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", dev_eui[0],
                     dev_eui[1], dev_eui[2], dev_eui[3], dev_eui[4], dev_eui[5],
                     dev_eui[6], dev_eui[7]);
      lv_label_set_text(dev_info_deveui, deveui_str);
    }
    /* FW version is static text, already set in create_lvgl_screens */
    /* Format button counters in hexadecimal */
    if (dev_info_counters) {
      char counters_str[64];
      uint32_t cnt[NUM_BUTTONS];
      int pos = 0;
      for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
        (void)button_counter_store_get(i, &cnt[i]);
      }
      /* Display values in hex (lowercase, as 0x%lx). */
      pos = snprintf(counters_str, sizeof(counters_str), "B0:%lx",
                     (unsigned long)cnt[0]);
      for (uint8_t i = 1;
           i < NUM_BUTTONS && pos < (int)(sizeof(counters_str) - 8); i++) {
        pos += snprintf(counters_str + pos, sizeof(counters_str) - pos,
                        " B%u:%lx", (unsigned)i, (unsigned long)cnt[i]);
      }
      lv_label_set_text(dev_info_counters, counters_str);
    }
    scr_to_show = screen_device_info;
    break;
  }
  case JOB_FULL_REFRESH:
    LOG_INF("[EPD] full refresh");
    /* Show current screen again to force refresh */
    switch (current_screen) {
    case DISPLAY_SCREEN_LOGO:
      scr_to_show = screen_logo;
      break;
    case DISPLAY_SCREEN_LAST_CLEANED:
      scr_to_show = screen_last_cleaned;
      break;
    case DISPLAY_SCREEN_THANKS:
      scr_to_show = screen_thanks;
      break;
    case DISPLAY_SCREEN_CLEANING:
      scr_to_show = screen_cleaning;
      break;
    case DISPLAY_SCREEN_CONNECTING:
      scr_to_show = screen_connecting;
      break;
    case DISPLAY_SCREEN_DEVICE_INFO:
      scr_to_show = screen_device_info;
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }

  if (scr_to_show) {
    lv_obj_clear_flag(scr_to_show, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(scr_to_show);
    lv_obj_invalidate(scr_to_show);
  }
}

/** If we dequeue a job then bail before the normal tail, unblock sync callers
 * (SMF would otherwise wait forever on cleaning/thanks/last_cleaned/logo). */
static void display_signal_sync_aborted(enum display_job_type type) {
  switch (type) {
  case JOB_SHOW_THANKS:
    if (atomic_get(&thanks_sync_waiting) != 0) {
      atomic_set(&thanks_sync_waiting, 0);
      atomic_set(&vote_ui_busy, 0);
      atomic_set(&vote_ack_pending, 0);
      k_sem_give(&thanks_done_sem);
    }
    break;
  case JOB_SHOW_LOGO:
    if (atomic_get(&logo_sync_waiting) != 0) {
      atomic_set(&logo_sync_waiting, 0);
      k_sem_give(&logo_done_sem);
    }
    break;
  case JOB_SHOW_LAST_CLEANED:
    if (atomic_get(&last_cleaned_sync_waiting) != 0) {
      atomic_set(&last_cleaned_sync_waiting, 0);
      k_sem_give(&last_cleaned_done_sem);
    }
    break;
  case JOB_SHOW_CLEANING:
    if (atomic_get(&cleaning_sync_waiting) != 0) {
      atomic_set(&cleaning_sync_waiting, 0);
      k_sem_give(&cleaning_done_sem);
    }
    break;
  default:
    break;
  }
}

static void display_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  struct display_job job = {0};
  const struct device *display;

  if (k_msgq_get(&display_jobq, &job, K_NO_WAIT) != 0) {
    return;
  }

  rail_manager_request_3v3a();

  display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display)) {
    LOG_ERR("[EPD] display device not ready");
    display_signal_sync_aborted(job.type);
    LOG_WRN("[EPD] released blocked sync waiter(s) for job type=%u",
            (unsigned)job.type);
    rail_manager_release_3v3a();
    return;
  }

  atomic_set(&display_epd_spi_busy, 1);

  uint32_t epoch = 0;
  switch (job.type) {
  case JOB_SHOW_LOGO:
    current_screen = DISPLAY_SCREEN_LOGO;
    do_render(display, JOB_SHOW_LOGO, 0);
    break;
  case JOB_SHOW_LAST_CLEANED: {
    /* EPD fast update often doesn't fully switch when changing to different
     * content (LOGO or CONNECTING -> LAST_CLEANED); force full refresh. */
    bool need_full_refresh = (current_screen == DISPLAY_SCREEN_LOGO ||
                              current_screen == DISPLAY_SCREEN_CONNECTING);
    if (need_full_refresh) {
      ssd1683_set_fast_update(display, false);
    }
    current_screen = DISPLAY_SCREEN_LAST_CLEANED;
    if (atomic_get(&cleaning_timer_active_atomic)) {
      k_timer_stop(&cleaning_timer);
      atomic_set(&cleaning_timer_active_atomic, 0);
    }
    /* If cleaning timer expired, update last cleaned time from RTC */
    if (atomic_get(&cleaning_timer_expired_atomic)) {
      atomic_set(&cleaning_timer_expired_atomic, 0);
      if (rtc_get_epoch_seconds(&epoch) == 0 && epoch != 0) {
        (void)last_cleaned_store_set(epoch);
        LOG_INF("[EPD] Cleaning auto-revert, last cleaned = %u", epoch);
      } else {
        (void)last_cleaned_store_get(&epoch);
        LOG_WRN("[EPD] Cleaning auto-revert, RTC unavailable");
      }
    } else {
      k_mutex_lock(&pending_epoch_mutex, K_FOREVER);
      epoch = pending_last_cleaned_epoch;
      pending_last_cleaned_epoch = 0;
      k_mutex_unlock(&pending_epoch_mutex);
      if (epoch != 0) {
        (void)last_cleaned_store_set(epoch);
      } else {
        (void)last_cleaned_store_get(&epoch);
        if (epoch == 0) {
          (void)rtc_get_epoch_seconds(&epoch);
        }
      }
    }
    do_render(display, JOB_SHOW_LAST_CLEANED, epoch);
    if (need_full_refresh) {
      ssd1683_set_fast_update(display, true);
    }
    break;
  }
  case JOB_SHOW_THANKS:
    current_screen = DISPLAY_SCREEN_THANKS;
    k_timer_stop(&thanks_timer);
    do_render(display, JOB_SHOW_THANKS, 0);
    k_timer_start(&thanks_timer, K_MSEC(EPD_THANKS_DISPLAY_MS), K_NO_WAIT);
    break;
  case JOB_SHOW_CLEANING: {
    current_screen = DISPLAY_SCREEN_CLEANING;
    bool timer_was_running = (atomic_get(&cleaning_timer_active_atomic) != 0);
    atomic_set(&cleaning_timer_active_atomic, 1);
    do_render(display, JOB_SHOW_CLEANING, 0);
    if (!timer_was_running) {
      k_timer_start(&cleaning_timer, K_MSEC(EPD_CLEANING_AUTO_REVERT_MS),
                    K_NO_WAIT);
      LOG_INF("[EPD] Cleaning timer started (%u min)",
              EPD_CLEANING_REVERT_MINUTES);
    } else {
      LOG_DBG("[EPD] Cleaning screen reshown, timer kept running");
    }
    break;
  }
  case JOB_SHOW_CONNECTING:
    current_screen = DISPLAY_SCREEN_CONNECTING;
    do_render(display, JOB_SHOW_CONNECTING, 0);
    break;
  case JOB_SHOW_DEVICE_INFO:
    current_screen = DISPLAY_SCREEN_DEVICE_INFO;
    do_render(display, JOB_SHOW_DEVICE_INFO, 0);
    break;
  case JOB_FULL_REFRESH:
    ssd1683_set_fast_update(display, false);
    do_render(display, JOB_FULL_REFRESH, 0);
    break;
  default:
    do_render(display, (enum display_job_type)job.type, job.epoch);
    break;
  }

  /* Force LVGL to render and flush to the EPD. A single lv_task_handler() is
   * unreliable in DIRECT mode — it may defer the flush to the next tick. */
  force_lvgl_flush();
  LOG_DBG("[EPD] flush done for job type=%u", job.type);

  /* Restore fast update after a full (slow) refresh */
  if (job.type == JOB_FULL_REFRESH) {
    ssd1683_set_fast_update(display, true);
  }

  if (job.type == JOB_SHOW_LAST_CLEANED && atomic_get(&vote_ack_pending)) {
    atomic_set(&vote_ack_pending, 0);
    atomic_set(&vote_ui_busy, 0);
  }
  /* Public vote: thanks_timer normally queues LAST_CLEANED to finish the ack
   * flow. When cleaning is active it queues CLEANING instead — same vote_ack
   * latch must clear or further votes stay blocked (display_is_public_vote_ui_busy).
   */
  if (job.type == JOB_SHOW_CLEANING && atomic_get(&vote_ack_pending)) {
    atomic_set(&vote_ack_pending, 0);
    atomic_set(&vote_ui_busy, 0);
  }

  /* Signal sync caller AFTER force_lvgl_flush: EPD SPI is fully done. */
  if (job.type == JOB_SHOW_THANKS && atomic_get(&thanks_sync_waiting)) {
    atomic_set(&thanks_sync_waiting, 0);
    k_sem_give(&thanks_done_sem);
  }
  if (job.type == JOB_SHOW_LOGO && atomic_get(&logo_sync_waiting)) {
    atomic_set(&logo_sync_waiting, 0);
    k_sem_give(&logo_done_sem);
  }
  if (job.type == JOB_SHOW_LAST_CLEANED &&
      atomic_get(&last_cleaned_sync_waiting)) {
    atomic_set(&last_cleaned_sync_waiting, 0);
    k_sem_give(&last_cleaned_done_sem);
  }
  if (job.type == JOB_SHOW_CLEANING &&
      atomic_get(&cleaning_sync_waiting)) {
    atomic_set(&cleaning_sync_waiting, 0);
    k_sem_give(&cleaning_done_sem);
  }

  atomic_set(&display_epd_spi_busy, 0);

  rail_manager_release_3v3a();

  /* If more jobs queued, run the next one with its proper delay. Follow-up
   * jobs that arrive while this handler ran got delay=0 from the queue, but
   * they should also apply DISPLAY_WORK_DELAY_MS for LoRa RX protection. */
  if (k_msgq_num_used_get(&display_jobq) > 0) {
    (void)k_work_reschedule(&display_work, K_MSEC(DISPLAY_WORK_DELAY_MS));
  }
}

static void enqueue_job(enum display_job_type type, uint32_t epoch) {
  struct display_job job = {.type = type, .epoch = epoch};
  if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
    LOG_WRN("[EPD] job queue full, drop type=%u", type);
    return;
  }
  /* k_work_schedule preserves an existing deadline (returns 0 if already
   * pending) so a later enqueue doesn't push back an earlier job's render.
   * Returns 1 if newly submitted.
   *
   * Edge case: handler is currently running (not pending). k_work_schedule
   * returns 0 ("already busy"). The handler's tail-check will see the new
   * job and reschedule itself at 100 ms. As a safety net, if the handler has
   * already passed its tail-check, force a short reschedule.
   *
   * EPD and LoRa share SPI. Defer EPD until past LoRa RX windows to avoid
   * missing downlinks. LOGO, CONNECTING: 0 delay (no uplink racing). THANKS:
   * DISPLAY_THANKS_DELAY_MS (button triggers uplink). Others:
   * DISPLAY_WORK_DELAY_MS. */
  uint32_t delay_ms;
  if (type == JOB_SHOW_LOGO || type == JOB_SHOW_CONNECTING) {
    delay_ms = 0U;
  } else {
    /* THANKS from button uses display_show_thanks_sync (0 delay, blocks).
     * Async THANKS (if used) and others use DISPLAY_WORK_DELAY_MS. */
    delay_ms = DISPLAY_WORK_DELAY_MS;
  }
  int ret = k_work_schedule(&display_work, K_MSEC(delay_ms));
  if (ret == 0 && !k_work_delayable_is_pending(&display_work)) {
    /* Work is running right now and has not yet re-scheduled itself. Force
     * reschedule with the FULL delay so LAST_CLEANED / CLEANING don't fire
     * at the 200ms fallback and land in LoRa RX windows. */
    (void)k_work_reschedule(&display_work, K_MSEC(delay_ms));
  }
}

#endif /* EPD_ENABLED */

int display_manager_init(void) {
#if EPD_ENABLED
  struct display_capabilities caps;

  k_work_init_delayable(&display_work, display_work_handler);
  pending_last_cleaned_epoch = 0;
  atomic_set(&cleaning_timer_active_atomic, 0);
  atomic_set(&cleaning_timer_expired_atomic, 0);

  /* Get display device */
  lvgl_display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(lvgl_display_dev)) {
    LOG_ERR("[EPD] display device not ready");
    return -ENODEV;
  }

  /* Get display capabilities */
  display_get_capabilities(lvgl_display_dev, &caps);

  /* Initialize LVGL */
  lv_init();

  /* Get or create default display */
  lvgl_display = lv_display_get_default();
  if (lvgl_display == NULL) {
    /* Create LVGL display if default doesn't exist */
    lvgl_display = lv_display_create(caps.x_resolution, caps.y_resolution);
    if (lvgl_display == NULL) {
      LOG_ERR("[EPD] failed to create LVGL display");
      return -ENOMEM;
    }
    lv_display_set_default(lvgl_display);
  } else {
    /* Set resolution if using existing display */
    lv_display_set_resolution(lvgl_display, caps.x_resolution,
                              caps.y_resolution);
  }

  /* Set display flush callback */
  lv_display_set_flush_cb(lvgl_display, lvgl_flush_cb);
  lv_display_set_user_data(lvgl_display, (void *)lvgl_display_dev);

  /* Set color format for monochrome (I1 = 1 bit per pixel) */
  lv_display_set_color_format(lvgl_display, LV_COLOR_FORMAT_I1);

  /* Set draw buffers
   * NOTE: LVGL 9.3 has known issues with I1 format in PARTIAL mode (wrong
   * coordinates). Using DIRECT mode with full-screen buffers as workaround.
   * Buffer size for monochrome: (width * height / 8) + 8 bytes for palette
   * Stride for monochrome: (width + 7) / 8 bytes per row
   */
  uint32_t stride_bytes =
      (caps.x_resolution + 7) / 8; /* Bytes per row: (400+7)/8 = 50 */
  uint32_t full_buf_size =
      stride_bytes * caps.y_resolution + 8; /* Full screen + palette */

  /* Ensure buffer is large enough for DIRECT mode */
  if (sizeof(lvgl_buf1) < full_buf_size) {
    LOG_ERR("[EPD] Buffer too small: have=%d need=%d", sizeof(lvgl_buf1),
            full_buf_size);
    return -ENOMEM;
  }

  LOG_INF("[EPD] Setting buffers: size=%d stride=%d mode=DIRECT",
          sizeof(lvgl_buf1), stride_bytes);
  lv_display_set_buffers_with_stride(lvgl_display, lvgl_buf1, lvgl_buf2,
                                     full_buf_size, stride_bytes,
                                     LV_DISPLAY_RENDER_MODE_DIRECT);

  /* Ensure this display is the default before creating screens */
  lv_display_set_default(lvgl_display);

  /* Create all screens (they will be created on the default display) */
  create_lvgl_screens();

  LOG_INF("display_manager init (EPD enabled, LVGL initialized)");
#else
  LOG_INF("display_manager init (EPD disabled)");
#endif
  return 0;
}

void display_show_logo(void) {
#if EPD_ENABLED
  enqueue_job(JOB_SHOW_LOGO, 0);
#endif
}

void display_show_logo_sync(void) {
#if EPD_ENABLED
  struct display_job job = {.type = JOB_SHOW_LOGO, .epoch = 0};
  if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
    LOG_WRN("[EPD] logo sync: job queue full");
    return;
  }
  atomic_set(&logo_sync_waiting, 1);
  (void)k_work_schedule(&display_work, K_MSEC(0));
  if (k_sem_take(&logo_done_sem, K_MSEC(30000)) != 0) {
    LOG_WRN("[EPD] logo sync timeout");
  }
  atomic_set(&logo_sync_waiting, 0);
#endif
}

void display_wait_until_spi_idle(uint32_t timeout_ms) {
#if EPD_ENABLED
  uint32_t const start = k_uptime_get_32();
  for (;;) {
    if (atomic_get(&display_epd_spi_busy) == 0 &&
        k_msgq_num_used_get(&display_jobq) == 0 &&
        !k_work_delayable_is_pending(&display_work)) {
      break;
    }
    if (timeout_ms != UINT32_MAX &&
        (k_uptime_get_32() - start) >= timeout_ms) {
      LOG_WRN("[EPD] SPI idle wait timeout (%u ms)", (unsigned)timeout_ms);
      break;
    }
    k_msleep(20);
  }
#endif
}

void display_show_last_cleaned(void) {
#if EPD_ENABLED
  enqueue_job(JOB_SHOW_LAST_CLEANED, 0);
#endif
}

void display_show_last_cleaned_sync(void) {
#if EPD_ENABLED
  struct display_job job = {.type = JOB_SHOW_LAST_CLEANED, .epoch = 0};
  if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
    LOG_WRN("[EPD] last_cleaned sync: job queue full");
    return;
  }
  atomic_set(&last_cleaned_sync_waiting, 1);
  (void)k_work_schedule(&display_work, K_MSEC(0));
  if (k_sem_take(&last_cleaned_done_sem, K_MSEC(10000)) != 0) {
    LOG_WRN("[EPD] last_cleaned sync timeout");
  }
  atomic_set(&last_cleaned_sync_waiting, 0);
#endif
}

void display_show_thanks(void) {
#if EPD_ENABLED
  enqueue_job(JOB_SHOW_THANKS, 0);
#endif
}

/** Show THANKS and block until EPD render completes. Use for button press:
 * EPD first (all SPI work), then LoRa/EEPROM run with clear SPI for RX. */
void display_show_thanks_sync(void) {
#if EPD_ENABLED
  struct display_job job = {.type = JOB_SHOW_THANKS, .epoch = 0};
  if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
    return;
  }
  atomic_set(&vote_ui_busy, 1);
  atomic_set(&vote_ack_pending, 1);
  atomic_set(&thanks_sync_waiting, 1);
  (void)k_work_schedule(&display_work, K_MSEC(0));
  if (k_sem_take(&thanks_done_sem, K_MSEC(10000)) != 0) {
    LOG_WRN("[EPD] thanks sync timeout; clearing vote UI busy");
    atomic_set(&vote_ui_busy, 0);
    atomic_set(&vote_ack_pending, 0);
  }
  atomic_set(&thanks_sync_waiting, 0);
#endif
}

void display_show_cleaning(void) {
#if EPD_ENABLED
  enqueue_job(JOB_SHOW_CLEANING, 0);
#endif
}

void display_show_cleaning_sync(void) {
#if EPD_ENABLED
  struct display_job job = {.type = JOB_SHOW_CLEANING, .epoch = 0};
  if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
    LOG_WRN("[EPD] cleaning sync: job queue full");
    return;
  }
  atomic_set(&cleaning_sync_waiting, 1);
  (void)k_work_schedule(&display_work, K_MSEC(0));
  if (k_sem_take(&cleaning_done_sem, K_MSEC(10000)) != 0) {
    LOG_WRN("[EPD] cleaning sync timeout");
  }
  atomic_set(&cleaning_sync_waiting, 0);
#endif
}

void display_show_connecting(void) {
#if EPD_ENABLED
  enqueue_job(JOB_SHOW_CONNECTING, 0);
#endif
}

void display_show_device_info(void) {
#if EPD_ENABLED
  enqueue_job(JOB_SHOW_DEVICE_INFO, 0);
#endif
}

void display_set_pending_last_cleaned(uint32_t epoch) {
#if EPD_ENABLED
  k_mutex_lock(&pending_epoch_mutex, K_FOREVER);
  pending_last_cleaned_epoch = epoch;
  k_mutex_unlock(&pending_epoch_mutex);
#endif
}

void display_set_pending_last_cleaned_and_apply(uint32_t epoch) {
#if EPD_ENABLED
  k_mutex_lock(&pending_epoch_mutex, K_FOREVER);
  pending_last_cleaned_epoch = epoch;
  k_mutex_unlock(&pending_epoch_mutex);
  if (current_screen != DISPLAY_SCREEN_THANKS) {
    enqueue_job(JOB_SHOW_LAST_CLEANED, 0);
  }
#endif
}

void display_request_full_refresh(void) {
#if EPD_ENABLED
  enqueue_job(JOB_FULL_REFRESH, 0);
#endif
}

bool display_is_public_vote_ui_busy(void) {
#if EPD_ENABLED
  return atomic_get(&vote_ui_busy) != 0;
#else
  return false;
#endif
}
