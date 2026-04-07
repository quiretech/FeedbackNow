/**
 * Display manager: EPD screen jobs and work queue. When EPD_ENABLED=0, no-ops.
 */
#include "display_manager.h"
#include "last_cleaned_store.h"
#include "rail_manager.h"
#include "rtc.h"
#include "sys_config.h"

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if EPD_ENABLED
#include "ssd1683.h"
#include <lvgl.h>

/* Font declarations */
LV_FONT_DECLARE(roboto_28);
LV_FONT_DECLARE(roboto_36);
LV_FONT_DECLARE(roboto_bold_42);
#endif

LOG_MODULE_REGISTER(display_mgr, CONFIG_LOG_DEFAULT_LEVEL);

#if EPD_ENABLED

#define DISPLAY_JOB_QUEUE_SIZE 8
#define DISPLAY_JOB_ALIGN 4

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

static struct k_work display_work;
static volatile uint32_t pending_last_cleaned_epoch; /* 0 = none */
static bool cleaning_timer_active;
static enum display_screen_id current_screen = DISPLAY_SCREEN_LOGO;

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

static void thanks_timer_expiry(struct k_timer *timer) {
  ARG_UNUSED(timer);
  display_show_last_cleaned();
}

static void cleaning_timer_expiry(struct k_timer *timer) {
  ARG_UNUSED(timer);
  cleaning_timer_active = false;
  uint32_t now = 0;
  (void)rtc_get_epoch_seconds(&now);
  if (now != 0) {
    (void)last_cleaned_store_set(now);
  }
  display_show_last_cleaned();
}

K_TIMER_DEFINE(thanks_timer, thanks_timer_expiry, NULL);
K_TIMER_DEFINE(cleaning_timer, cleaning_timer_expiry, NULL);

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

  /* Screen: LOGO */
  screen_logo = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_logo, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_logo, LV_OPA_COVER, LV_PART_MAIN);
  label = lv_label_create(screen_logo);
  lv_label_set_text(label, "FeedbackNow\nFlexBox");
  lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(label);
  lv_obj_add_flag(screen_logo, LV_OBJ_FLAG_HIDDEN);

  /* Screen: LAST_CLEANED */
  screen_last_cleaned = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_last_cleaned, lv_color_white(),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_last_cleaned, LV_OPA_COVER, LV_PART_MAIN);
  label = lv_label_create(screen_last_cleaned);
  lv_label_set_text(label, "LAST CLEANED");
  lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
  last_cleaned_label = lv_label_create(screen_last_cleaned);
  lv_label_set_text(last_cleaned_label, "2026/01/01 00:00");
  lv_obj_set_style_text_font(last_cleaned_label, &roboto_36, LV_PART_MAIN);
  lv_obj_set_style_text_color(last_cleaned_label, lv_color_black(),
                              LV_PART_MAIN);
  lv_obj_align_to(last_cleaned_label, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
  lv_obj_add_flag(screen_last_cleaned, LV_OBJ_FLAG_HIDDEN);

  /* Screen: THANKS */
  screen_thanks = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_thanks, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_thanks, LV_OPA_COVER, LV_PART_MAIN);
  label = lv_label_create(screen_thanks);
  lv_label_set_text(label, "Thanks\nfor your\nFeedback!");
  lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(label);
  lv_obj_add_flag(screen_thanks, LV_OBJ_FLAG_HIDDEN);

  /* Screen: CLEANING */
  screen_cleaning = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_cleaning, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_cleaning, LV_OPA_COVER, LV_PART_MAIN);
  label = lv_label_create(screen_cleaning);
  lv_label_set_text(label, "Cleaning\nIn Progress");
  lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(label);
  lv_obj_add_flag(screen_cleaning, LV_OBJ_FLAG_HIDDEN);

  /* Screen: CONNECTING */
  screen_connecting = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_connecting, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_connecting, LV_OPA_COVER, LV_PART_MAIN);
  label = lv_label_create(screen_connecting);
  lv_label_set_text(label, "Connecting...");
  lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(label);
  lv_obj_add_flag(screen_connecting, LV_OBJ_FLAG_HIDDEN);

  /* Screen: DEVICE_INFO */
  screen_device_info = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_device_info, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_device_info, LV_OPA_COVER, LV_PART_MAIN);
  label = lv_label_create(screen_device_info);
  lv_label_set_text(label, "Device Info");
  lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(label);
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
  uint32_t d = epoch_s / 86400U;
  uint32_t t = epoch_s % 86400U;
  uint32_t h = t / 3600U;
  uint32_t m = (t % 3600U) / 60U;
  /* Simple day to y/m/d (approximate from 1970-01-01). */
  uint32_t y = 1970 + (d / 365U);
  uint32_t rem = d % 365U;
  uint32_t mo = 1 + (rem / 31U);
  uint32_t day = 1 + (rem % 31U);
  if (mo > 12) {
    mo = 12;
  }
  if (day > 28) {
    day = 28;
  }
  (void)snprintf(buf, buf_len, "%04u/%02u/%02u %02u:%02u", (unsigned)y,
                 (unsigned)mo, (unsigned)day, (unsigned)h, (unsigned)m);
}

static void do_render(const struct device *display, enum display_job_type type,
                      uint32_t epoch) {
  int ret;
  char ts[32];
  lv_obj_t *scr_to_show = NULL;

  if (display == NULL || !device_is_ready(display)) {
    LOG_ERR("[EPD] display not ready");
    return;
  }

  ret = display_blanking_off(display);
  if (ret < 0) {
    LOG_ERR("[EPD] blanking_off failed: %d", ret);
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
    format_epoch_yyyymmdd_hhmm(epoch, ts, sizeof(ts));
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
  case JOB_SHOW_DEVICE_INFO:
    LOG_INF("[EPD] show DEVICE_INFO");
    scr_to_show = screen_device_info;
    break;
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
    rail_manager_release_3v3a();
    return;
  }

  /* Process LVGL tasks before rendering */
  lv_task_handler();

  uint32_t epoch = 0;
  switch (job.type) {
  case JOB_SHOW_LOGO:
    current_screen = DISPLAY_SCREEN_LOGO;
    do_render(display, JOB_SHOW_LOGO, 0);
    break;
  case JOB_SHOW_LAST_CLEANED:
    current_screen = DISPLAY_SCREEN_LAST_CLEANED;
    if (cleaning_timer_active) {
      k_timer_stop(&cleaning_timer);
      cleaning_timer_active = false;
    }
    if (pending_last_cleaned_epoch != 0) {
      epoch = pending_last_cleaned_epoch;
      pending_last_cleaned_epoch = 0;
      (void)last_cleaned_store_set(epoch);
    } else {
      (void)last_cleaned_store_get(&epoch);
      if (epoch == 0) {
        (void)rtc_get_epoch_seconds(&epoch);
      }
    }
    do_render(display, JOB_SHOW_LAST_CLEANED, epoch);
    break;
  case JOB_SHOW_THANKS:
    current_screen = DISPLAY_SCREEN_THANKS;
    k_timer_stop(&thanks_timer);
    do_render(display, JOB_SHOW_THANKS, 0);
    k_timer_start(&thanks_timer, K_MSEC(EPD_THANKS_DISPLAY_MS), K_NO_WAIT);
    break;
  case JOB_SHOW_CLEANING:
    current_screen = DISPLAY_SCREEN_CLEANING;
    if (cleaning_timer_active) {
      k_timer_stop(&cleaning_timer);
    }
    cleaning_timer_active = true;
    do_render(display, JOB_SHOW_CLEANING, 0);
    k_timer_start(&cleaning_timer, K_MSEC(EPD_CLEANING_AUTO_REVERT_MS),
                  K_NO_WAIT);
    break;
  case JOB_SHOW_CONNECTING:
    current_screen = DISPLAY_SCREEN_CONNECTING;
    do_render(display, JOB_SHOW_CONNECTING, 0);
    break;
  case JOB_SHOW_DEVICE_INFO:
    current_screen = DISPLAY_SCREEN_DEVICE_INFO;
    do_render(display, JOB_SHOW_DEVICE_INFO, 0);
    break;
  case JOB_FULL_REFRESH:
    do_render(display, JOB_FULL_REFRESH, 0);
    break;
  default:
    do_render(display, (enum display_job_type)job.type, job.epoch);
    break;
  }

  /* Process LVGL tasks after rendering to flush display */
  lv_task_handler();

  rail_manager_release_3v3a();

  /* If more jobs, resubmit work */
  if (k_msgq_num_used_get(&display_jobq) > 0) {
    k_work_submit(&display_work);
  }
}

static void enqueue_job(enum display_job_type type, uint32_t epoch) {
  struct display_job job = {.type = type, .epoch = epoch};
  if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
    LOG_WRN("[EPD] job queue full, drop type=%u", type);
    return;
  }
  k_work_submit(&display_work);
}

#endif /* EPD_ENABLED */

int display_manager_init(void) {
#if EPD_ENABLED
  struct display_capabilities caps;

  k_work_init(&display_work, display_work_handler);
  pending_last_cleaned_epoch = 0;
  cleaning_timer_active = false;

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

void display_show_last_cleaned(void) {
#if EPD_ENABLED
  enqueue_job(JOB_SHOW_LAST_CLEANED, 0);
#endif
}

void display_show_thanks(void) {
#if EPD_ENABLED
  enqueue_job(JOB_SHOW_THANKS, 0);
#endif
}

void display_show_cleaning(void) {
#if EPD_ENABLED
  enqueue_job(JOB_SHOW_CLEANING, 0);
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
  pending_last_cleaned_epoch = epoch;
#endif
}

void display_set_pending_last_cleaned_and_apply(uint32_t epoch) {
#if EPD_ENABLED
  pending_last_cleaned_epoch = epoch;
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
