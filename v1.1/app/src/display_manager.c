  /**
  * Display manager: EPD screen jobs and work queue. When EPD_ENABLED=0, no-ops.
  */
  #include "display_manager.h"
  #include "button_counter_store.h"
  #include "eui_keys.h"
  #include "last_cleaned_store.h"
  #include "lora_link_stats.h"
  #include "rail_manager.h"
  #include "rtc.h"
  #include "sys_config.h"
  #include "tz_offset_store.h"

  #include <ctype.h>
  #include <stdio.h>
  #include <string.h>
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
  LV_FONT_DECLARE(roboto_bold_36);
  LV_FONT_DECLARE(roboto_bold_42);
  LV_IMG_DECLARE(boot_screen);
#if EPD_LOCALE_FR_BITMAPS
  LV_IMG_DECLARE(thanks_fr);
  LV_IMG_DECLARE(cleaning_fr);
#else
  LV_IMG_DECLARE(thanks_en);
  LV_IMG_DECLARE(cleaning_en);
#endif
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
    JOB_SHOW_DL_CUSTOM_MESSAGE,
#if EPD_INSTALL_INFO_SCREEN
    JOB_SHOW_INSTALL_INFO,
#endif
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
  static lv_obj_t *screen_dl_custom;
  static lv_obj_t *dl_custom_msg_label;
#if EPD_INSTALL_INFO_SCREEN
  static lv_obj_t *screen_install_info;
#endif
  static lv_obj_t *last_cleaned_label; /* Label on screen_last_cleaned */
  /* Device Info screen labels */
  static lv_obj_t *dev_info_heading;
  static lv_obj_t *dev_info_unit_value;
  static lv_obj_t *dev_info_deveui;
  static lv_obj_t *dev_info_fw;
  static lv_obj_t *dev_info_counters;
  static lv_obj_t *dev_info_footer;
#if EPD_DEVICE_INFO_QR
  static lv_obj_t *dev_info_qr;
  /* flex-grow row: manufacturer (left) + QR (right), bottoms aligned */
  static lv_obj_t *dev_info_qr_slot;
#endif
#if EPD_INSTALL_INFO_SCREEN
  /* Install Info screen labels + QR code */
  static lv_obj_t *install_link_label;   /* PREFIX + tier (sys_config macros) */
  static lv_obj_t *install_margin_label; /* "margin  12 dB" */
  static lv_obj_t *install_gateways_label; /* "gateways  3" */
  static lv_obj_t *install_unit_label;   /* DEVICE_UNIT_ID_STRING */
  static lv_obj_t *install_deveui_label; /* DevEUI hex */
  static lv_obj_t *install_qr;           /* lv_qrcode */
#endif

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

  static void dl_custom_revert_timer_expiry(struct k_timer *timer) {
    ARG_UNUSED(timer);
    /* Match thanks_timer: reopen cleaning session if cleaner timer runs. */

    if (atomic_get(&cleaning_timer_active_atomic)) {

      enqueue_job(JOB_SHOW_CLEANING, 0);

      return;

    }

    enqueue_job(JOB_SHOW_LAST_CLEANED, 0);

  }

  K_TIMER_DEFINE(thanks_timer, thanks_timer_expiry, NULL);
  K_TIMER_DEFINE(cleaning_timer, cleaning_timer_expiry, NULL);
  K_TIMER_DEFINE(dl_custom_revert_timer, dl_custom_revert_timer_expiry, NULL);

  static char dl_custom_msg_buf[DISPLAY_DL_CUSTOM_TEXT_MAX + 1];
  static K_MUTEX_DEFINE(dl_custom_msg_mutex);

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

  K_SEM_DEFINE(device_info_done_sem, 0, 1);
  static atomic_t device_info_sync_waiting = ATOMIC_INIT(0);

#if EPD_INSTALL_INFO_SCREEN
  K_SEM_DEFINE(install_info_done_sem, 0, 1);
  static atomic_t install_info_sync_waiting = ATOMIC_INIT(0);
#endif

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
    lv_img_set_src(img_logo, &boot_screen);
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
    lv_label_set_text(label, EPD_TEXT_LAST_CLEANED_HEADLINE);


    #if EPD_LOCALE_FR_BITMAPS
    lv_obj_set_style_text_font(label, &roboto_bold_36, LV_PART_MAIN);
    #else
    lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
    #endif
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);

    last_cleaned_label = lv_label_create(cont);
    lv_label_set_text(last_cleaned_label, "2026/01/01 00:00");
    lv_obj_set_style_text_font(last_cleaned_label, &roboto_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(last_cleaned_label, lv_color_black(),
                                LV_PART_MAIN);

    lv_obj_add_flag(screen_last_cleaned, LV_OBJ_FLAG_HIDDEN);

    /* Screen: THANKS — full-screen bitmap; locale via EPD_LOCALE_FR_BITMAPS */
    screen_thanks = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_thanks, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_thanks, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *img_thanks = lv_img_create(screen_thanks);
#if EPD_LOCALE_FR_BITMAPS
    lv_img_set_src(img_thanks, &thanks_fr);
#else
    lv_img_set_src(img_thanks, &thanks_en);
#endif
    lv_obj_center(img_thanks);
    lv_obj_add_flag(screen_thanks, LV_OBJ_FLAG_HIDDEN);

    /* Screen: CLEANING — full-screen bitmap (cleaning_en / cleaning_fr) */
    screen_cleaning = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_cleaning, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_cleaning, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *img_cleaning = lv_img_create(screen_cleaning);
#if EPD_LOCALE_FR_BITMAPS
    lv_img_set_src(img_cleaning, &cleaning_fr);
#else
    lv_img_set_src(img_cleaning, &cleaning_en);
#endif
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
    lv_label_set_text(label, EPD_TEXT_CONNECTING);
    lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);

    lv_obj_add_flag(screen_connecting, LV_OBJ_FLAG_HIDDEN);

    /* DL 0x99: fullscreen centered text (Roboto 36, max ~3 lines clipped). */
    screen_dl_custom = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(screen_dl_custom, lv_color_white(), LV_PART_MAIN);

    lv_obj_set_style_bg_opa(screen_dl_custom, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *cont_dl = lv_obj_create(screen_dl_custom);

    lv_obj_set_size(cont_dl, 400, 300);

    lv_obj_center(cont_dl);

    lv_obj_set_style_bg_color(cont_dl, lv_color_white(), LV_PART_MAIN);

    lv_obj_set_style_bg_opa(cont_dl, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_border_width(cont_dl, 0, LV_PART_MAIN);

    lv_obj_set_style_pad_left(cont_dl, 16, LV_PART_MAIN);

    lv_obj_set_style_pad_right(cont_dl, 16, LV_PART_MAIN);

    lv_obj_set_style_pad_top(cont_dl, 12, LV_PART_MAIN);

    lv_obj_set_style_pad_bottom(cont_dl, 12, LV_PART_MAIN);

    lv_obj_set_flex_flow(cont_dl, LV_FLEX_FLOW_COLUMN);

    lv_obj_set_flex_align(cont_dl, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    dl_custom_msg_label = lv_label_create(cont_dl);

    lv_label_set_long_mode(dl_custom_msg_label, LV_LABEL_LONG_WRAP);

    lv_label_set_text(dl_custom_msg_label, "");

    lv_obj_set_width(dl_custom_msg_label, 368);

    lv_obj_set_style_text_font(dl_custom_msg_label, &roboto_36, LV_PART_MAIN);

    lv_obj_set_style_text_color(dl_custom_msg_label, lv_color_black(),
                                LV_PART_MAIN);

    lv_obj_set_style_text_align(dl_custom_msg_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    lv_obj_set_height(dl_custom_msg_label,

                      lv_font_get_line_height(&roboto_36) * 3);

    lv_obj_add_flag(screen_dl_custom, LV_OBJ_FLAG_HIDDEN);

    /* DEVICE_INFO (400×300): title + rule; unit/DevEUI/counters/fw (left);
     * flexible band with QR top-right in band; footer left.
     */


    screen_device_info = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_device_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_device_info, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *cont_devinfo = lv_obj_create(screen_device_info);
    lv_obj_set_size(cont_devinfo, 400, 300);
    lv_obj_align(cont_devinfo, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cont_devinfo, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont_devinfo, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont_devinfo, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(cont_devinfo, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(cont_devinfo, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(cont_devinfo, 12, LV_PART_MAIN);
    /* Slight lift: manufacturer sits in the QR row; keep a hair above panel edge. */
    lv_obj_set_style_pad_bottom(cont_devinfo, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont_devinfo, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_devinfo, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    /* Tight row gap: panel is 300px tall — avoid clipping footer + QR bottom. */
    lv_obj_set_style_pad_row(cont_devinfo, 4, LV_PART_MAIN);

    {

      const lv_coord_t dev_body_w = 372; /* 400 - 14px padding each side */

      dev_info_heading = lv_label_create(cont_devinfo);
      lv_label_set_text(dev_info_heading, EPD_TEXT_BRAND_TITLE);
      lv_obj_set_width(dev_info_heading, dev_body_w);
      lv_label_set_long_mode(dev_info_heading, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(dev_info_heading, &roboto_bold_36, LV_PART_MAIN);
      lv_obj_set_style_text_color(dev_info_heading, lv_color_black(),
                                  LV_PART_MAIN);
      lv_obj_set_style_text_align(dev_info_heading, LV_TEXT_ALIGN_LEFT,
                                  LV_PART_MAIN);

      lv_obj_t *dev_info_rule = lv_obj_create(cont_devinfo);
      lv_obj_set_size(dev_info_rule, dev_body_w, 1);
      lv_obj_set_style_bg_color(dev_info_rule, lv_color_black(), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(dev_info_rule, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(dev_info_rule, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(dev_info_rule, 0, LV_PART_MAIN);
      lv_obj_set_style_margin_top(dev_info_rule, 2, LV_PART_MAIN);
      lv_obj_set_style_margin_bottom(dev_info_rule, 4, LV_PART_MAIN);

      dev_info_unit_value = lv_label_create(cont_devinfo);
      lv_label_set_text(dev_info_unit_value, DEVICE_UNIT_ID_STRING);
      lv_obj_set_width(dev_info_unit_value, dev_body_w);
      lv_label_set_long_mode(dev_info_unit_value, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(dev_info_unit_value, &roboto_28, LV_PART_MAIN);
      lv_obj_set_style_text_color(dev_info_unit_value, lv_color_black(),
                                  LV_PART_MAIN);
      lv_obj_set_style_text_align(dev_info_unit_value, LV_TEXT_ALIGN_LEFT,
                                  LV_PART_MAIN);

      /* DevEUI + counters + fw: same body font (roboto_20) on 1bpp panel. */

      dev_info_deveui = lv_label_create(cont_devinfo);
      lv_label_set_text(dev_info_deveui, "00:00:00:00:00:00:00:00");
      lv_obj_set_width(dev_info_deveui, dev_body_w);
      lv_label_set_long_mode(dev_info_deveui, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(dev_info_deveui, &roboto_20, LV_PART_MAIN);
      lv_obj_set_style_text_color(dev_info_deveui, lv_color_black(),
                                  LV_PART_MAIN);
      lv_obj_set_style_text_align(dev_info_deveui, LV_TEXT_ALIGN_LEFT,
                                  LV_PART_MAIN);

      dev_info_counters = lv_label_create(cont_devinfo);
      lv_label_set_text(dev_info_counters, "b0:0  b1:0  b2:0  b3:0  b4:0  b5:0");
      lv_obj_set_width(dev_info_counters, dev_body_w);
      lv_label_set_long_mode(dev_info_counters, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(dev_info_counters, &roboto_20, LV_PART_MAIN);
      lv_obj_set_style_text_color(dev_info_counters, lv_color_black(),
                                  LV_PART_MAIN);
      lv_obj_set_style_text_align(dev_info_counters, LV_TEXT_ALIGN_LEFT,
                                  LV_PART_MAIN);

      dev_info_fw = lv_label_create(cont_devinfo);
      lv_label_set_text(dev_info_fw, EPD_TEXT_FW_PREFIX FW_VERSION_STRING);
      lv_obj_set_width(dev_info_fw, dev_body_w);
      lv_label_set_long_mode(dev_info_fw, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(dev_info_fw, &roboto_20, LV_PART_MAIN);
      lv_obj_set_style_text_color(dev_info_fw, lv_color_black(), LV_PART_MAIN);
      lv_obj_set_style_text_align(dev_info_fw, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

#if EPD_DEVICE_INFO_QR
      dev_info_qr_slot = lv_obj_create(cont_devinfo);

      lv_obj_set_width(dev_info_qr_slot, dev_body_w);
      lv_obj_set_flex_grow(dev_info_qr_slot, 1);
      /* Slightly taller floor so the QR can grow upward into the spacer. */
      lv_obj_set_style_min_height(dev_info_qr_slot, 94, LV_PART_MAIN);
      lv_obj_set_style_pad_all(dev_info_qr_slot, 0, LV_PART_MAIN);
      lv_obj_set_style_border_width(dev_info_qr_slot, 0, LV_PART_MAIN);
      lv_obj_set_style_bg_opa(dev_info_qr_slot, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_clear_flag(dev_info_qr_slot, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_set_flex_flow(dev_info_qr_slot, LV_FLEX_FLOW_ROW);
      /* Footer left + QR right; CROSS=END aligns bottoms (no footer clipped under panel). */
      lv_obj_set_flex_align(dev_info_qr_slot, LV_FLEX_ALIGN_START,
                            LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);

      dev_info_footer = lv_label_create(dev_info_qr_slot);
      lv_label_set_text(dev_info_footer, EPD_TEXT_MANUFACTURER);
      lv_label_set_long_mode(dev_info_footer, LV_LABEL_LONG_WRAP);
      lv_obj_set_flex_grow(dev_info_footer, 1);
      lv_obj_set_style_pad_right(dev_info_footer, 8, LV_PART_MAIN);
      lv_obj_set_style_text_font(dev_info_footer, &roboto_20, LV_PART_MAIN);
      lv_obj_set_style_text_color(dev_info_footer, lv_color_black(),
                                  LV_PART_MAIN);
      lv_obj_set_style_text_align(dev_info_footer, LV_TEXT_ALIGN_LEFT,
                                  LV_PART_MAIN);

      dev_info_qr = lv_qrcode_create(dev_info_qr_slot);
      lv_qrcode_set_size(dev_info_qr, 104);
      lv_qrcode_set_dark_color(dev_info_qr, lv_color_black());
      lv_qrcode_set_light_color(dev_info_qr, lv_color_white());
      lv_obj_clear_flag(dev_info_qr, LV_OBJ_FLAG_SCROLLABLE);
#else
      dev_info_footer = lv_label_create(cont_devinfo);
      lv_label_set_text(dev_info_footer, EPD_TEXT_MANUFACTURER);

      lv_obj_set_width(dev_info_footer, dev_body_w);
      lv_label_set_long_mode(dev_info_footer, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(dev_info_footer, &roboto_20, LV_PART_MAIN);
      lv_obj_set_style_text_color(dev_info_footer, lv_color_black(),
                                  LV_PART_MAIN);
      lv_obj_set_style_text_align(dev_info_footer, LV_TEXT_ALIGN_LEFT,
                                  LV_PART_MAIN);
#endif
    }

    lv_obj_add_flag(screen_device_info, LV_OBJ_FLAG_HIDDEN);

#if EPD_INSTALL_INFO_SCREEN
    /* Screen: INSTALL_INFO (MVP) — 400x300.
    *   y=  6..26   header : EPD_TEXT_BRAND_TITLE (left) | unit id (right) [roboto_20]
    *   y= 44..86   link banner : PREFIX + tier (EPD_INSTALL_LINK_*)           [roboto_bold_42]
    *   y= 96       divider line
    *   y=104..132  metrics : EPD_INSTALL_GATEWAYS_FMT / margin fmt/strings   [roboto_28]
    *   y=150..280  QR          : 130-px canvas, horizontally centered
    * The QR payload carries full device identity (unit, DevEUI, FW, margin,
    * gateways), so we don't reprint it alongside the QR — the header already
    * gives a human-readable FW/HW cross-check for the installer.
    */
    screen_install_info = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_install_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_install_info, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen_install_info, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen_install_info, 0, LV_PART_MAIN);

    /* Header row */
    lv_obj_t *header = lv_obj_create(screen_install_info);
    lv_obj_set_size(header, 400, 28);
    lv_obj_set_pos(header, 0, 6);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(header, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(header, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(header, 0, LV_PART_MAIN);

    lv_obj_t *install_title = lv_label_create(header);
    lv_label_set_text(install_title, EPD_TEXT_BRAND_TITLE);
    lv_obj_set_style_text_font(install_title, &roboto_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(install_title, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(install_title, LV_ALIGN_LEFT_MID, 0, 0);

    install_unit_label = lv_label_create(header);
    lv_label_set_text(install_unit_label, DEVICE_UNIT_ID_STRING);
    lv_obj_set_style_text_font(install_unit_label, &roboto_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(install_unit_label, lv_color_black(),
                                LV_PART_MAIN);
    lv_obj_align(install_unit_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* --- Link quality banner (dominant glyph) --- */
    install_link_label = lv_label_create(screen_install_info);
    {
      char pending_link[80];
      (void)snprintf(pending_link, sizeof(pending_link), "%s%s",
                    EPD_INSTALL_LINK_LABEL_PREFIX,
                    EPD_INSTALL_LINK_PENDING_PLACEHOLDER);
      lv_label_set_text(install_link_label, pending_link);
    }
    lv_obj_set_style_text_font(install_link_label, &roboto_bold_42, LV_PART_MAIN);
    lv_obj_set_style_text_color(install_link_label, lv_color_black(),
                                LV_PART_MAIN);
    lv_obj_align(install_link_label, LV_ALIGN_TOP_MID, 0, 44);

    /* --- Divider --- */
    lv_obj_t *install_divider = lv_obj_create(screen_install_info);
    lv_obj_set_size(install_divider, 360, 1);
    lv_obj_set_pos(install_divider, 20, 96);
    lv_obj_set_style_bg_color(install_divider, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(install_divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(install_divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(install_divider, 0, LV_PART_MAIN);

    /* --- Metrics row: gateways left, margin right on the SAME line.
    * Transparent container just to anchor both ends within a shared baseline
    * without flex layout (keeps refresh behaviour deterministic). */
    lv_obj_t *metrics_row = lv_obj_create(screen_install_info);
    lv_obj_set_size(metrics_row, 360, 30);
    lv_obj_set_pos(metrics_row, 20, 104);
    lv_obj_set_style_bg_opa(metrics_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(metrics_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(metrics_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(metrics_row, LV_OBJ_FLAG_SCROLLABLE);

    install_gateways_label = lv_label_create(metrics_row);
    {
      char gw0[48];
      (void)snprintf(gw0, sizeof(gw0), EPD_INSTALL_GATEWAYS_FMT, 0U);
      lv_label_set_text(install_gateways_label, gw0);
    }
    lv_obj_set_style_text_font(install_gateways_label, &roboto_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(install_gateways_label, lv_color_black(),
                                LV_PART_MAIN);
    lv_obj_align(install_gateways_label, LV_ALIGN_LEFT_MID, 0, 0);

    install_margin_label = lv_label_create(metrics_row);
    lv_label_set_text(install_margin_label, EPD_INSTALL_MARGIN_TEXT_EMPTY);
    lv_obj_set_style_text_font(install_margin_label, &roboto_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(install_margin_label, lv_color_black(),
                                LV_PART_MAIN);
    lv_obj_align(install_margin_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* --- QR code: 130 px, horizontally centered under the metrics row. Size
    * set here allocates the canvas; refresh_install_info_dynamic() calls
    * lv_qrcode_update() to re-encode in place without reallocating. */
    install_qr = lv_qrcode_create(screen_install_info);
    lv_qrcode_set_size(install_qr, 130);
    lv_qrcode_set_dark_color(install_qr, lv_color_black());
    lv_qrcode_set_light_color(install_qr, lv_color_white());
    lv_obj_align(install_qr, LV_ALIGN_TOP_MID, 0, 150);

    /* MVP: DevEUI lives only in the QR payload (unit id is now in the header).
    * Leaving this pointer NULL tells refresh_install_info_dynamic() to skip
    * the DevEUI label write. */
    install_deveui_label = NULL;

    lv_obj_add_flag(screen_install_info, LV_OBJ_FLAG_HIDDEN);
#endif
  }

#if EPD_INSTALL_INFO_SCREEN
  /**
  * Map best demod margin to a 4-tier install label. When we haven't received
  * any LinkCheckAns (samples == 0), return WEAK with a "no-data" hint so the
  * installer knows the gateway didn't answer — which is the conservative
  * reading of the situation.
  */
  static const char *install_link_label_build(
      const lora_link_stats_snapshot_t *ls) {
    static char link_buf[96];
    /* No LinkCheckAns: same label shape as weakest tier so installer sees one
    * predictable string across locales (words from sys_config.h). */
    if (ls->samples == 0 || ls->best_demod_margin == LORA_LINK_STATS_MARGIN_NONE) {
      (void)snprintf(link_buf, sizeof(link_buf), "%s%s",
                    EPD_INSTALL_LINK_LABEL_PREFIX,
                    EPD_INSTALL_LINK_QUALITY_WEAK);
      return link_buf;
    }
    int16_t m = ls->best_demod_margin;
    const char *tier = EPD_INSTALL_LINK_QUALITY_WEAK;
    if (m >= INSTALL_LINK_MARGIN_EXCELLENT_DB) {
      tier = EPD_INSTALL_LINK_QUALITY_EXCELLENT;
    } else if (m >= INSTALL_LINK_MARGIN_GOOD_DB) {
      tier = EPD_INSTALL_LINK_QUALITY_GOOD;
    } else if (m >= INSTALL_LINK_MARGIN_FAIR_DB) {
      tier = EPD_INSTALL_LINK_QUALITY_FAIR;
    }
    (void)snprintf(link_buf, sizeof(link_buf), "%s%s",
                  EPD_INSTALL_LINK_LABEL_PREFIX, tier);
    return link_buf;
  }

  /**
  * Refresh install-info labels + re-encode the QR from current stats.
  *
  * QR payload format (pipe-delimited; installer app parses by splitting on '|'
  * and then '=' within each field):
  *   FBN|v=1|uid=<unit>|dev=<deveui_hex>|fw=<x.y.z>|m=<margin_db>|g=<nb_gw>
  *
  * When no LinkCheckAns has landed yet, m and g are -1/0; app still gets a
  * valid, scannable QR with device identity for commissioning logging.
  */
  static void refresh_install_info_dynamic(void) {
    if (screen_install_info == NULL) {
      return;
    }

    lora_link_stats_snapshot_t ls;
    memset(&ls, 0, sizeof(ls));
    ls.last_demod_margin = LORA_LINK_STATS_MARGIN_NONE;
    ls.best_demod_margin = LORA_LINK_STATS_MARGIN_NONE;
    (void)lora_link_stats_get(&ls);

    /* Link quality headline */
    if (install_link_label) {
      lv_label_set_text(install_link_label, install_link_label_build(&ls));
    }

    /* Metrics — show best margin / best gateway count (installers care about
    * peak capability of this location, not a single noisy sample). */
    if (install_margin_label) {
      char buf[48];
      if (ls.samples > 0 &&
          ls.best_demod_margin != LORA_LINK_STATS_MARGIN_NONE) {
        (void)snprintf(buf, sizeof(buf), EPD_INSTALL_MARGIN_FMT_WITH_VALUE,
                      (int)ls.best_demod_margin);
      } else {
        (void)snprintf(buf, sizeof(buf), "%s", EPD_INSTALL_MARGIN_TEXT_EMPTY);
      }
      lv_label_set_text(install_margin_label, buf);
    }
    if (install_gateways_label) {
      char buf[48];
      (void)snprintf(buf, sizeof(buf), EPD_INSTALL_GATEWAYS_FMT,
                    (unsigned)ls.best_nb_gateways);
      lv_label_set_text(install_gateways_label, buf);
    }

    /* DevEUI hex (no colons — saves QR chars). Unit id comes straight from
    * sys_config.h (kept in sync with eui_registry by gen_euis.py). */
    static const uint8_t dev_eui[] = LORAWAN_DEV_EUI;
    char deveui_colon[32];
    char deveui_nocolon[20];
    (void)snprintf(deveui_colon, sizeof(deveui_colon),
                  "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", dev_eui[0],
                  dev_eui[1], dev_eui[2], dev_eui[3], dev_eui[4], dev_eui[5],
                  dev_eui[6], dev_eui[7]);
    (void)snprintf(deveui_nocolon, sizeof(deveui_nocolon),
                  "%02x%02x%02x%02x%02x%02x%02x%02x", dev_eui[0], dev_eui[1],
                  dev_eui[2], dev_eui[3], dev_eui[4], dev_eui[5], dev_eui[6],
                  dev_eui[7]);

    if (install_unit_label) {
      lv_label_set_text(install_unit_label, DEVICE_UNIT_ID_STRING);
    }
    if (install_deveui_label) {
      lv_label_set_text(install_deveui_label, deveui_colon);
    }

    /* QR payload. Keep short: QR version scales with length, and a bigger
    * version means coarser modules on the 130 px canvas (harder to scan). */
    if (install_qr) {
      char payload[128];
      int margin_for_qr = (ls.samples > 0 &&
                          ls.best_demod_margin != LORA_LINK_STATS_MARGIN_NONE)
                              ? (int)ls.best_demod_margin
                              : -1;
      int n = snprintf(payload, sizeof(payload),
                      "FBN|v=1|uid=%s|dev=%s|fw=%s|m=%d|g=%u",
                      DEVICE_UNIT_ID_STRING, deveui_nocolon, FW_VERSION_STRING,
                      margin_for_qr, (unsigned)ls.best_nb_gateways);
      if (n < 0 || n >= (int)sizeof(payload)) {
        LOG_WRN("[EPD] install QR payload truncated (len=%d)", n);
        n = (int)sizeof(payload) - 1;
      }
      lv_result_t qr_res = lv_qrcode_update(install_qr, payload, (uint32_t)n);
      if (qr_res != LV_RESULT_OK) {
        LOG_WRN("[EPD] lv_qrcode_update failed (res=%d len=%d)", (int)qr_res, n);
      } else {
        LOG_DBG("[EPD] install QR: %s", payload);
      }
    }

    LOG_INF("[EPD] install_info: %s margin=%d gw=%u samples=%u",
            install_link_label_build(&ls), (int)ls.best_demod_margin,
            (unsigned)ls.best_nb_gateways, (unsigned)ls.samples);
  }
#endif

#if EPD_DEVICE_INFO_QR
  /** Largest square QR in the slot row: height-limited but not wider than space
   * remaining beside the footer label (two passes so flex footer width settles). */
  static void device_info_qr_fit_in_slot(void) {
    if (dev_info_qr == NULL || dev_info_qr_slot == NULL) {

      return;

    }
    lv_obj_t *col = lv_obj_get_parent(dev_info_qr_slot);

    for (int pass = 0; pass < 2; pass++) {

      if (col != NULL) {

        lv_obj_update_layout(col);

      }

      lv_coord_t cw = lv_obj_get_content_width(dev_info_qr_slot);

      lv_coord_t ch = lv_obj_get_content_height(dev_info_qr_slot);

      if (cw < 24 || ch < 24) {

        return;

      }
      lv_coord_t gap = 8;
      lv_coord_t footer_w = 0;

      if (dev_info_footer != NULL) {

        footer_w = lv_obj_get_width(dev_info_footer);

      }

      if (pass == 0 && footer_w < 64) {

        /* First layout pass: footer flex width not settled yet ~ reserve text room. */

        footer_w = 148;

      }
      lv_coord_t avail_w = cw - footer_w - gap;

      if (avail_w < 48) {

        avail_w = 48;

      }
      /* Height-limited square; inset from slot content height. */

      lv_coord_t z = ch - 2;

      if (z > avail_w) {

        z = avail_w;

      }

      if (z < 48) {

        z = 48;

      }

      if (z > 152) {

        z = 152;

      }

      lv_qrcode_set_size(dev_info_qr, (uint32_t)z);

    }

  }
#endif

  /* Refresh unit id, DevEUI, button counters, fw line; optional Device Info QR.
  *
  * When EPD_DEVICE_INFO_QR: QR payload (pipe-separated):
  *   FBN|v=2|brand=...|uid=...|dev=...|cnt=...|fw=...|mfg=...
  * mfg=. are sent as _ so phone QR apps do not treat domains as URLs.
  */
  static void refresh_device_info_dynamic(void) {
    static const uint8_t dev_eui[] = LORAWAN_DEV_EUI;
    char deveui_colon[40];
#if EPD_DEVICE_INFO_QR
    char deveui_nocolon[20];
#endif

    (void)snprintf(
        deveui_colon, sizeof(deveui_colon),
        "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", dev_eui[0], dev_eui[1],
        dev_eui[2], dev_eui[3], dev_eui[4], dev_eui[5], dev_eui[6], dev_eui[7]);
#if EPD_DEVICE_INFO_QR
    (void)snprintf(deveui_nocolon, sizeof(deveui_nocolon),
                  "%02x%02x%02x%02x%02x%02x%02x%02x", dev_eui[0], dev_eui[1],
                  dev_eui[2], dev_eui[3], dev_eui[4], dev_eui[5], dev_eui[6],
                  dev_eui[7]);
#endif

    uint32_t cnt[NUM_BUTTONS];
    for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
      (void)button_counter_store_get(i, &cnt[i]);
    }

    char counters_str[64];
    int pos = snprintf(counters_str, sizeof(counters_str), "b0:%lx",
                      (unsigned long)cnt[0]);
    for (uint8_t i = 1;
        i < NUM_BUTTONS && pos < (int)(sizeof(counters_str) - 10); i++) {
      pos += snprintf(counters_str + pos, sizeof(counters_str) - pos,
                      "  b%u:%lx", (unsigned)i, (unsigned long)cnt[i]);
    }

#if EPD_DEVICE_INFO_QR
    char counters_qr[80];
    int qp = snprintf(counters_qr, sizeof(counters_qr), "b0:%lx",
                     (unsigned long)cnt[0]);
    for (uint8_t i = 1;
        i < NUM_BUTTONS && qp < (int)(sizeof(counters_qr) - 12); i++) {
      qp += snprintf(counters_qr + qp, sizeof(counters_qr) - qp, "_b%u:%lx",
                    (unsigned)i, (unsigned long)cnt[i]);
    }

#endif

    if (dev_info_unit_value) {
      char uid_lower[64];
      const char *uid = DEVICE_UNIT_ID_STRING;
      size_t i = 0;
      while (uid[i] != '\0' && i + 1U < sizeof(uid_lower)) {
        uid_lower[i] = (char)tolower((unsigned char)uid[i]);
        ++i;
      }
      uid_lower[i] = '\0';
      lv_label_set_text(dev_info_unit_value, uid_lower);
    }
    if (dev_info_deveui) {
      lv_label_set_text(dev_info_deveui, deveui_colon);
    }
    if (dev_info_counters) {
      lv_label_set_text(dev_info_counters, counters_str);
    }
    if (dev_info_fw) {
      char fwbuf[48];
      int n = snprintf(fwbuf, sizeof(fwbuf), "%s%s", EPD_TEXT_FW_PREFIX,
                        FW_VERSION_STRING);
      if (n > 0 && n < (int)sizeof(fwbuf)) {
        for (int j = 0; fwbuf[j] != '\0'; j++) {
          fwbuf[j] = (char)tolower((unsigned char)fwbuf[j]);
        }
      }
      lv_label_set_text(dev_info_fw, fwbuf);
    }

#if EPD_DEVICE_INFO_QR
    if (dev_info_qr) {
      char mfg_qr[40];
      const char *src = EPD_TEXT_MANUFACTURER;
      size_t k = 0;

      while (src[k] != '\0' && k + 1U < sizeof(mfg_qr)) {
        char c = src[k];
        mfg_qr[k] = (c == '.') ? '_' : c;
        k++;
      }

      mfg_qr[k] = '\0';
      char payload[192];
      int n = snprintf(payload, sizeof(payload),
                      "FBN|v=2|brand=%s|uid=%s|dev=%s|cnt=%s|fw=%s|mfg=%s",
                      EPD_TEXT_BRAND_TITLE, DEVICE_UNIT_ID_STRING,
                      deveui_nocolon, counters_qr, FW_VERSION_STRING,
                      mfg_qr);
      if (n < 0 || n >= (int)sizeof(payload)) {
        LOG_WRN("[EPD] device_info QR payload truncated or error (len=%d)", n);
      }
      device_info_qr_fit_in_slot();
      uint32_t qr_len = (uint32_t)strlen(payload);
      lv_result_t qr_res = lv_qrcode_update(dev_info_qr, payload, qr_len);
      if (qr_res != LV_RESULT_OK) {
        LOG_WRN("[EPD] device_info lv_qrcode_update failed (res=%u len=%u)",
                (unsigned)qr_res, (unsigned)qr_len);
      } else {
        LOG_DBG("[EPD] device_info QR: %s", payload);
      }
    }
#endif

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

    /* Power on EPD. Skip the blanking_off dance (which issues a DISPLAY_UPDATE
    * quick-resume that can trip into cold-start when the panel is in deep sleep
    * or lost rail) when the controller reports it is still powered — i.e. we
    * just came off a partial refresh and the rail keep-alive kept it warm. */
    if (ssd1683_is_powered_on(display)) {
      LOG_DBG("[EPD] panel already powered, skip blanking_off");
      ret = 0;
    } else {
      const int64_t t_blank = k_uptime_get();
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
      LOG_INF("[EPD] blanking_off in %lld ms",
              (long long)(k_uptime_get() - t_blank));
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
#if EPD_INSTALL_INFO_SCREEN
    if (screen_install_info) {
      lv_obj_add_flag(screen_install_info, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    if (screen_dl_custom) {
      lv_obj_add_flag(screen_dl_custom, LV_OBJ_FLAG_HIDDEN);
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
    case JOB_SHOW_DEVICE_INFO:
      LOG_INF("[EPD] show DEVICE_INFO");
      scr_to_show = screen_device_info;
      break;
    case JOB_SHOW_DL_CUSTOM_MESSAGE:
      LOG_INF("[EPD] show DL_CUSTOM_MESSAGE");
      if (dl_custom_msg_label != NULL) {
        k_mutex_lock(&dl_custom_msg_mutex, K_FOREVER);
        lv_label_set_text(dl_custom_msg_label, dl_custom_msg_buf);
        k_mutex_unlock(&dl_custom_msg_mutex);

      }
      scr_to_show = screen_dl_custom;

      break;

#if EPD_INSTALL_INFO_SCREEN
    case JOB_SHOW_INSTALL_INFO:
      LOG_INF("[EPD] show INSTALL_INFO");
      scr_to_show = screen_install_info;
      break;
#endif
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
      case DISPLAY_SCREEN_DL_CUSTOM:
        scr_to_show = screen_dl_custom;
        break;
#if EPD_INSTALL_INFO_SCREEN
      case DISPLAY_SCREEN_INSTALL_INFO:
        scr_to_show = screen_install_info;
        break;
#endif
      default:
        break;
      }
      break;
    default:
      break;
    }

    if (scr_to_show == screen_device_info) {
      refresh_device_info_dynamic();
#if EPD_INSTALL_INFO_SCREEN
    } else if (scr_to_show == screen_install_info) {
      refresh_install_info_dynamic();
#endif
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
    case JOB_SHOW_DEVICE_INFO:
      if (atomic_get(&device_info_sync_waiting) != 0) {
        atomic_set(&device_info_sync_waiting, 0);
        k_sem_give(&device_info_done_sem);
      }
      break;
#if EPD_INSTALL_INFO_SCREEN
    case JOB_SHOW_INSTALL_INFO:
      if (atomic_get(&install_info_sync_waiting) != 0) {
        atomic_set(&install_info_sync_waiting, 0);
        k_sem_give(&install_info_done_sem);
      }
      break;
#endif
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
      k_timer_stop(&dl_custom_revert_timer);
      current_screen = DISPLAY_SCREEN_LOGO;
      do_render(display, JOB_SHOW_LOGO, 0);
      break;
    case JOB_SHOW_LAST_CLEANED: {
      k_timer_stop(&dl_custom_revert_timer);
      /* EPD fast update often doesn't fully switch when changing to different
      * content (LOGO or CONNECTING -> LAST_CLEANED); force full refresh. */
      bool need_full_refresh = (current_screen == DISPLAY_SCREEN_LOGO ||
                                current_screen == DISPLAY_SCREEN_CONNECTING ||
                                current_screen == DISPLAY_SCREEN_DL_CUSTOM);
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
      k_timer_stop(&dl_custom_revert_timer);
      current_screen = DISPLAY_SCREEN_THANKS;
      k_timer_stop(&thanks_timer);
      do_render(display, JOB_SHOW_THANKS, 0);
      k_timer_start(&thanks_timer, K_MSEC(EPD_THANKS_DISPLAY_MS), K_NO_WAIT);
      break;
    case JOB_SHOW_CLEANING: {
      k_timer_stop(&dl_custom_revert_timer);
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
      k_timer_stop(&dl_custom_revert_timer);
      current_screen = DISPLAY_SCREEN_CONNECTING;
      do_render(display, JOB_SHOW_CONNECTING, 0);
      break;
    case JOB_SHOW_DEVICE_INFO:
      k_timer_stop(&dl_custom_revert_timer);
      current_screen = DISPLAY_SCREEN_DEVICE_INFO;
      do_render(display, JOB_SHOW_DEVICE_INFO, 0);
      break;
    case JOB_SHOW_DL_CUSTOM_MESSAGE: {
      k_timer_stop(&dl_custom_revert_timer);

      current_screen = DISPLAY_SCREEN_DL_CUSTOM;

      do_render(display, JOB_SHOW_DL_CUSTOM_MESSAGE, 0);

      uint32_t hold = job.epoch;

      if (hold == 0U) {

        hold = DL_CUSTOM_TEXT_DEFAULT_MINUTES;

      }

      if (hold > 1440U) {

        hold = 1440U;

      }

      k_timer_start(&dl_custom_revert_timer, K_MINUTES(hold), K_NO_WAIT);

      LOG_INF("[EPD] DL custom message hold %u min", (unsigned)hold);

      break;

    }

#if EPD_INSTALL_INFO_SCREEN
    case JOB_SHOW_INSTALL_INFO: {
      k_timer_stop(&dl_custom_revert_timer);
      /* Coming from LOGO / CONNECTING: fast-update ghosts badly against the
      * dense install layout (QR blocks, large fonts). Do one full refresh so
      * the installer sees crisp contrast. Restored after render below. */
      bool need_full_refresh = (current_screen == DISPLAY_SCREEN_LOGO ||
                                current_screen == DISPLAY_SCREEN_CONNECTING ||
                                current_screen == DISPLAY_SCREEN_DL_CUSTOM);
      if (need_full_refresh) {
        ssd1683_set_fast_update(display, false);
      }
      current_screen = DISPLAY_SCREEN_INSTALL_INFO;
      do_render(display, JOB_SHOW_INSTALL_INFO, 0);
      if (need_full_refresh) {
        ssd1683_set_fast_update(display, true);
      }
      break;
    }
#endif
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
    if (job.type == JOB_SHOW_DEVICE_INFO &&
        atomic_get(&device_info_sync_waiting)) {
      atomic_set(&device_info_sync_waiting, 0);
      k_sem_give(&device_info_done_sem);
    }
#if EPD_INSTALL_INFO_SCREEN
    if (job.type == JOB_SHOW_INSTALL_INFO &&
        atomic_get(&install_info_sync_waiting)) {
      atomic_set(&install_info_sync_waiting, 0);
      k_sem_give(&install_info_done_sem);
    }
#endif

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
    * missing downlinks. LOGO, CONNECTING, DEVICE_INFO: 0 delay (boot/join/staff
    * UI; no fresh uplink on that path). THANKS: DISPLAY_THANKS_DELAY_MS (button
    * triggers uplink). Others: DISPLAY_WORK_DELAY_MS. */
    uint32_t delay_ms;
    if (type == JOB_SHOW_LOGO || type == JOB_SHOW_CONNECTING ||
        type == JOB_SHOW_DEVICE_INFO
#if EPD_INSTALL_INFO_SCREEN
        || type == JOB_SHOW_INSTALL_INFO
#endif
        ) {
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

  void display_show_device_info_sync(void) {
  #if EPD_ENABLED
    struct display_job job = {.type = JOB_SHOW_DEVICE_INFO, .epoch = 0};
    if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
      LOG_WRN("[EPD] device_info sync: job queue full");
      return;
    }
    atomic_set(&device_info_sync_waiting, 1);
    (void)k_work_schedule(&display_work, K_MSEC(0));
    if (k_sem_take(&device_info_done_sem, K_MSEC(10000)) != 0) {
      LOG_WRN("[EPD] device_info sync timeout");
    }
    atomic_set(&device_info_sync_waiting, 0);
  #endif
  }

  void display_show_dl_custom_message(const char *text, uint32_t hold_minutes) {
  #if EPD_ENABLED
    if (text == NULL) {
      text = "";
    }
    k_mutex_lock(&dl_custom_msg_mutex, K_FOREVER);
    strncpy(dl_custom_msg_buf, text, DISPLAY_DL_CUSTOM_TEXT_MAX);
    dl_custom_msg_buf[DISPLAY_DL_CUSTOM_TEXT_MAX] = '\0';
    k_mutex_unlock(&dl_custom_msg_mutex);
    enqueue_job(JOB_SHOW_DL_CUSTOM_MESSAGE, hold_minutes);
  #else
    ARG_UNUSED(text);
    ARG_UNUSED(hold_minutes);
  #endif
  }

#if EPD_ENABLED && EPD_INSTALL_INFO_SCREEN
  void display_show_install_info(void) { enqueue_job(JOB_SHOW_INSTALL_INFO, 0); }

  void display_show_install_info_sync(void) {
    struct display_job job = {.type = JOB_SHOW_INSTALL_INFO, .epoch = 0};
    if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
      LOG_WRN("[EPD] install_info sync: job queue full");
      return;
    }
    atomic_set(&install_info_sync_waiting, 1);
    (void)k_work_schedule(&display_work, K_MSEC(0));
    /* Full refresh + QR encode can take several seconds on SSD1683; give a
    * generous timeout. Mirrors logo_sync's 30s cap for boot-class screens. */
    if (k_sem_take(&install_info_done_sem, K_MSEC(30000)) != 0) {
      LOG_WRN("[EPD] install_info sync timeout");
    }
    atomic_set(&install_info_sync_waiting, 0);
  }
#endif

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
