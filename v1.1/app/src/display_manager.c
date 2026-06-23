  /**
  * Display manager: EPD screen jobs and work queue. When EPD_ENABLED=0, no-ops.
  */
  #include "display_manager.h"
  #include "log_fmt.h"
  #include "button_counter_store.h"
  #include "eui_keys.h"
  #include "last_cleaned_store.h"
  #include "lora_app.h"
  #include "lora_link_stats.h"
  #include "battery_adc.h"
  #include "rail_manager.h"
  #include "rtc.h"
  #include "sys_config.h"
  #include "tz_offset_store.h"

  #include <ctype.h>
  #include <stdio.h>
  #include <string.h>
  #include <time.h>
  #include <zephyr/kernel.h>
  #include <zephyr/logging/log.h>
  #include <zephyr/sys/atomic.h>

  #if EPD_ENABLED
  #include <zephyr/device.h>
  #include <zephyr/devicetree.h>
  #include <zephyr/drivers/display.h>
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
#if EPD_LOCALE == EPD_LOCALE_FR
  LV_IMG_DECLARE(thanks_fr);
  LV_IMG_DECLARE(cleaning_fr);
#elif EPD_LOCALE == EPD_LOCALE_DE
  LV_IMG_DECLARE(thanks_de);
  LV_IMG_DECLARE(cleaning_de);
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
    JOB_SHOW_DEVICE_STATUS,
    JOB_SHOW_DL_CUSTOM_MESSAGE,
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
  static lv_obj_t *screen_device_status;
  static lv_obj_t *screen_dl_custom;
  static lv_obj_t *dl_custom_msg_label;
  static lv_obj_t *last_cleaned_label;
  /* Device status screen */
  static lv_obj_t *ds_brand;
  static lv_obj_t *ds_unit_ra;
  static lv_obj_t *ds_eui_la;
  static lv_obj_t *ds_battery_ra;
  static lv_obj_t *ds_link_row;
  static lv_obj_t *ds_link_la;
  static lv_obj_t *ds_link_ra;
  static lv_obj_t *ds_metrics_row;
  static lv_obj_t *ds_metrics_la;
  static lv_obj_t *ds_metrics_ra;
  static lv_obj_t *ds_status_row;
  static lv_obj_t *ds_status_la;
  static lv_obj_t *ds_status_ra;
  static lv_obj_t *ds_b0;
  static lv_obj_t *ds_b1;
  static lv_obj_t *ds_b2;
  static lv_obj_t *ds_b3;
  static lv_obj_t *ds_b4;
  static lv_obj_t *ds_b5;
  static lv_obj_t *ds_mfg;
  static lv_obj_t *ds_fw;

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

  /* For display_show_connecting_sync: deliberate re-join before OTAA. */
  K_SEM_DEFINE(connecting_done_sem, 0, 1);
  static atomic_t connecting_sync_waiting = ATOMIC_INIT(0);

  /* For display_show_cleaning_sync: NFC check-in / immediate cleaning screen. */
  K_SEM_DEFINE(cleaning_done_sem, 0, 1);
  static atomic_t cleaning_sync_waiting = ATOMIC_INIT(0);

  K_SEM_DEFINE(device_status_done_sem, 0, 1);
  static atomic_t device_status_sync_waiting = ATOMIC_INIT(0);
  /** 0=best since boot; 1=latest Ans (user probe); 2=RX timeout (user). */
  static atomic_t device_status_link_mode = ATOMIC_INIT(0);

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
      LOG_ERR("display_write error: %d", ret);
    }

    lv_display_flush_ready(display);
  }

  #define EPD_STATUS_BODY_W 360

  static void epd_strtolower(char *dst, const char *src, size_t dst_len) {
    size_t i = 0;

    if (dst == NULL || dst_len == 0) {
      return;
    }
    if (src != NULL) {
      while (src[i] != '\0' && i + 1U < dst_len) {
        dst[i] = (char)tolower((unsigned char)src[i]);
        ++i;
      }
    }
    dst[i] = '\0';
  }

  static const char *epd_lora_region_str(void) {
#if defined(CONFIG_LORAMAC_REGION_EU868)
    return "eu868";
#elif defined(CONFIG_LORAMAC_REGION_US915)
    return "us915";
#else
    return "?";
#endif
  }

  static lv_obj_t *epd_hline_create(lv_obj_t *parent) {
    lv_obj_t *line = lv_obj_create(parent);

    lv_obj_set_size(line, EPD_STATUS_BODY_W, 1);
    lv_obj_set_style_bg_color(line, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(line, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_top(line, 2, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(line, 2, LV_PART_MAIN);
    return line;
  }

  static lv_obj_t *epd_status_lr_row_create(lv_obj_t *parent, lv_obj_t **la_out,
                                            lv_obj_t **ra_out) {
    lv_obj_t *row = lv_obj_create(parent);

    lv_obj_set_width(row, EPD_STATUS_BODY_W);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *la = lv_label_create(row);
    lv_obj_t *ra = lv_label_create(row);
    lv_obj_set_style_text_font(la, &roboto_20, LV_PART_MAIN);
    lv_obj_set_style_text_font(ra, &roboto_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(la, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ra, lv_color_black(), LV_PART_MAIN);
    if (la_out != NULL) {
      *la_out = la;
    }
    if (ra_out != NULL) {
      *ra_out = ra;
    }
    return row;
  }

  static lv_obj_t *epd_status_grid_label_create(lv_obj_t *grid,
                                                lv_text_align_t align) {
    lv_obj_t *lab = lv_label_create(grid);

    lv_obj_set_width(lab, LV_PCT(100));
    lv_obj_set_style_text_font(lab, &roboto_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_align(lab, align, LV_PART_MAIN);
    return lab;
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


#if EPD_LOCALE == EPD_LOCALE_EN
    lv_obj_set_style_text_font(label, &roboto_bold_42, LV_PART_MAIN);
#else
    lv_obj_set_style_text_font(label, &roboto_bold_36, LV_PART_MAIN);
#endif
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);

    last_cleaned_label = lv_label_create(cont);
    lv_label_set_text(last_cleaned_label, "2026/01/01 00:00");
    lv_obj_set_style_text_font(last_cleaned_label, &roboto_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(last_cleaned_label, lv_color_black(),
                                LV_PART_MAIN);

    lv_obj_add_flag(screen_last_cleaned, LV_OBJ_FLAG_HIDDEN);

    /* Screen: THANKS — full-screen bitmap; locale via EPD_LOCALE */
    screen_thanks = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_thanks, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_thanks, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *img_thanks = lv_img_create(screen_thanks);
#if EPD_LOCALE == EPD_LOCALE_FR
    lv_img_set_src(img_thanks, &thanks_fr);
#elif EPD_LOCALE == EPD_LOCALE_DE
    lv_img_set_src(img_thanks, &thanks_de);
#else
    lv_img_set_src(img_thanks, &thanks_en);
#endif
    lv_obj_center(img_thanks);
    lv_obj_add_flag(screen_thanks, LV_OBJ_FLAG_HIDDEN);

    /* Screen: CLEANING — full-screen bitmap (cleaning_en / cleaning_fr / cleaning_de) */
    screen_cleaning = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_cleaning, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_cleaning, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *img_cleaning = lv_img_create(screen_cleaning);
#if EPD_LOCALE == EPD_LOCALE_FR
    lv_img_set_src(img_cleaning, &cleaning_fr);
#elif EPD_LOCALE == EPD_LOCALE_DE
    lv_img_set_src(img_cleaning, &cleaning_de);
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

    lv_obj_set_style_text_font(dl_custom_msg_label, &roboto_bold_36, LV_PART_MAIN);

    lv_obj_set_style_text_color(dl_custom_msg_label, lv_color_black(),
                                LV_PART_MAIN);

    lv_obj_set_style_text_align(dl_custom_msg_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    lv_obj_set_height(dl_custom_msg_label,

                      lv_font_get_line_height(&roboto_36) * 3);

    lv_obj_add_flag(screen_dl_custom, LV_OBJ_FLAG_HIDDEN);

    /* DEVICE_STATUS (400×300): center-anchored column, lowercase copy. */
    screen_device_status = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_device_status, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_device_status, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *ds_root = lv_obj_create(screen_device_status);
    lv_obj_set_size(ds_root, 400, 300);
    lv_obj_center(ds_root);
    lv_obj_set_style_bg_color(ds_root, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ds_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ds_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ds_root, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_right(ds_root, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ds_root, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(ds_root, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(ds_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ds_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ds_root, 4, LV_PART_MAIN);

    (void)epd_status_lr_row_create(ds_root, &ds_brand, &ds_unit_ra);
    lv_obj_set_style_text_font(ds_brand, &roboto_bold_42, LV_PART_MAIN);
    lv_obj_set_style_text_font(ds_unit_ra, &roboto_36, LV_PART_MAIN);
    lv_label_set_text(ds_brand, EPD_TEXT_BRAND_TITLE);
    lv_label_set_text(ds_unit_ra, "---");

    (void)epd_hline_create(ds_root);

    (void)epd_status_lr_row_create(ds_root, &ds_eui_la, &ds_battery_ra);
    lv_obj_set_style_text_font(ds_eui_la, &roboto_36, LV_PART_MAIN);
    lv_obj_set_style_text_font(ds_battery_ra, &roboto_36, LV_PART_MAIN);
    lv_label_set_text(ds_eui_la, "---");
    lv_label_set_text(ds_battery_ra, "---");

    (void)epd_hline_create(ds_root);

    ds_link_row = epd_status_lr_row_create(ds_root, &ds_link_la, &ds_link_ra);
    lv_label_set_text(ds_link_la, EPD_STATUS_LABEL_LINK);
    lv_label_set_text(ds_link_ra, EPD_STATUS_VALUE_NONE);

    ds_metrics_row = epd_status_lr_row_create(ds_root, &ds_metrics_la, &ds_metrics_ra);
    lv_label_set_text(ds_metrics_la, EPD_STATUS_GATEWAYS_EMPTY);
    lv_label_set_text(ds_metrics_ra, EPD_STATUS_MARGIN_EMPTY);

    ds_status_row = epd_status_lr_row_create(ds_root, &ds_status_la, &ds_status_ra);
    lv_label_set_text(ds_status_la, EPD_STATUS_LABEL_STATUS);
    lv_label_set_text(ds_status_ra, EPD_STATUS_NOT_JOINED);
    lv_obj_add_flag(ds_status_row, LV_OBJ_FLAG_HIDDEN);

    (void)epd_hline_create(ds_root);

    {
      /* Counter grid (3×2): b0 b2 b4 / b1 b3 b5 */
      static const int32_t grid_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                             LV_GRID_FR(1),
                                             LV_GRID_TEMPLATE_LAST};
      static const int32_t grid_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT,
                                             LV_GRID_TEMPLATE_LAST};

      lv_obj_t *grid = lv_obj_create(ds_root);
      lv_obj_set_width(grid, EPD_STATUS_BODY_W);
      lv_obj_set_height(grid, LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_row(grid, 4, LV_PART_MAIN);
      lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_layout(grid, LV_LAYOUT_GRID);
      lv_obj_set_grid_dsc_array(grid, grid_col_dsc, grid_row_dsc);

      ds_b0 = epd_status_grid_label_create(grid, LV_TEXT_ALIGN_LEFT);
      lv_obj_set_grid_cell(ds_b0, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 0,
                           1);
      lv_label_set_text(ds_b0, "b0:0");

      ds_b2 = epd_status_grid_label_create(grid, LV_TEXT_ALIGN_CENTER);
      lv_obj_set_grid_cell(ds_b2, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 0,
                           1);
      lv_label_set_text(ds_b2, "b2:0");

      ds_b4 = epd_status_grid_label_create(grid, LV_TEXT_ALIGN_RIGHT);
      lv_obj_set_grid_cell(ds_b4, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_START, 0,
                           1);
      lv_label_set_text(ds_b4, "b4:0");

      ds_b1 = epd_status_grid_label_create(grid, LV_TEXT_ALIGN_LEFT);
      lv_obj_set_grid_cell(ds_b1, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 1,
                           1);
      lv_label_set_text(ds_b1, "b1:0");

      ds_b3 = epd_status_grid_label_create(grid, LV_TEXT_ALIGN_CENTER);
      lv_obj_set_grid_cell(ds_b3, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_START, 1,
                           1);
      lv_label_set_text(ds_b3, "b3:0");

      ds_b5 = epd_status_grid_label_create(grid, LV_TEXT_ALIGN_RIGHT);
      lv_obj_set_grid_cell(ds_b5, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_START, 1,
                           1);
      lv_label_set_text(ds_b5, "b5:0");
    }

    lv_obj_t *ds_spacer = lv_obj_create(ds_root);
    lv_obj_set_width(ds_spacer, 360);
    lv_obj_set_flex_grow(ds_spacer, 1);
    lv_obj_set_style_bg_opa(ds_spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ds_spacer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ds_spacer, LV_OBJ_FLAG_SCROLLABLE);

    (void)epd_status_lr_row_create(ds_root, &ds_mfg, &ds_fw);
    lv_label_set_text(ds_mfg, EPD_TEXT_MANUFACTURER);
    lv_label_set_text(ds_fw, EPD_STATUS_FW_PREFIX FW_VERSION_STRING);

    lv_obj_add_flag(screen_device_status, LV_OBJ_FLAG_HIDDEN);
  }

  static const char *device_status_link_tier_margin(int16_t margin) {
    if (margin == LORA_LINK_STATS_MARGIN_NONE) {
      return EPD_STATUS_LINK_NO_RESPONSE;
    }
    if (margin >= EPD_LINK_MARGIN_EXCELLENT_DB) {
      return EPD_STATUS_LINK_EXCELLENT;
    }
    if (margin >= EPD_LINK_MARGIN_GOOD_DB) {
      return EPD_STATUS_LINK_GOOD;
    }
    if (margin >= EPD_LINK_MARGIN_FAIR_DB) {
      return EPD_STATUS_LINK_FAIR;
    }
    return EPD_STATUS_LINK_WEAK;
  }

  static void device_status_counter_label_set(lv_obj_t *label, uint8_t id,
                                              uint32_t val) {
    char buf[24];

    (void)snprintf(buf, sizeof(buf), "b%u:%lu", (unsigned)id,
                   (unsigned long)val);
    epd_strtolower(buf, buf, sizeof(buf));
    lv_label_set_text(label, buf);
  }

  static void refresh_device_status_dynamic(void) {
    if (screen_device_status == NULL) {
      return;
    }

    static const uint8_t dev_eui[] = LORAWAN_DEV_EUI;
    char brand_lower[32];
    char unit_lower[48];
    char deveui_tail[8];
    char battery_buf[16];
    char fwbuf[32];
    char mfg_lower[40];
    char status_ra[48];
    char metrics_la[32];
    char metrics_ra[32];

    epd_strtolower(brand_lower, EPD_TEXT_BRAND_TITLE, sizeof(brand_lower));
    epd_strtolower(unit_lower, DEVICE_UNIT_ID_STRING, sizeof(unit_lower));
    epd_strtolower(mfg_lower, EPD_TEXT_MANUFACTURER, sizeof(mfg_lower));
    (void)snprintf(deveui_tail, sizeof(deveui_tail), "%02x%02x%02x",
                   dev_eui[5], dev_eui[6], dev_eui[7]);
    (void)snprintf(fwbuf, sizeof(fwbuf), "%s%s", EPD_STATUS_FW_PREFIX,
                   FW_VERSION_STRING);
    epd_strtolower(fwbuf, fwbuf, sizeof(fwbuf));

    if (ds_brand != NULL) {
      lv_label_set_text(ds_brand, brand_lower);
    }
    if (ds_unit_ra != NULL) {
      lv_label_set_text(ds_unit_ra, unit_lower);
    }
    if (ds_eui_la != NULL) {
      lv_label_set_text(ds_eui_la, deveui_tail);
    }
    int32_t battery_mv = 0;
    if (battery_adc_last_mv_get(&battery_mv) == 0) {
      (void)battery_adc_format_mv_display(battery_mv, battery_buf,
                                         sizeof(battery_buf));
    } else {
      (void)snprintf(battery_buf, sizeof(battery_buf), "--");
    }
    if (ds_battery_ra != NULL) {
      lv_label_set_text(ds_battery_ra, battery_buf);
    }
    if (ds_mfg != NULL) {
      lv_label_set_text(ds_mfg, mfg_lower);
    }
    if (ds_fw != NULL) {
      lv_label_set_text(ds_fw, fwbuf);
    }

    uint32_t cnt[NUM_BUTTONS];
    for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
      (void)button_counter_store_get(i, &cnt[i]);
    }
    device_status_counter_label_set(ds_b0, 0, cnt[0]);
    device_status_counter_label_set(ds_b1, 1, cnt[1]);
    device_status_counter_label_set(ds_b2, 2, cnt[2]);
    device_status_counter_label_set(ds_b3, 3, cnt[3]);
    device_status_counter_label_set(ds_b4, 4, cnt[4]);
    device_status_counter_label_set(ds_b5, 5, cnt[5]);

    const bool joined = lora_is_joined();

    if (!joined) {
      if (ds_link_row != NULL) {
        lv_obj_add_flag(ds_link_row, LV_OBJ_FLAG_HIDDEN);
      }
      if (ds_metrics_row != NULL) {
        lv_obj_add_flag(ds_metrics_row, LV_OBJ_FLAG_HIDDEN);
      }
      if (ds_status_row != NULL) {
        lv_obj_clear_flag(ds_status_row, LV_OBJ_FLAG_HIDDEN);
      }
      if (ds_status_la != NULL) {
        lv_label_set_text(ds_status_la, EPD_STATUS_LABEL_STATUS);
      }
      if (ds_status_ra != NULL) {
        lv_label_set_text(ds_status_ra, EPD_STATUS_NOT_JOINED);
      }
      LOG_DBG("epd device_status not_joined");
      return;
    }

    if (ds_link_row != NULL) {
      lv_obj_clear_flag(ds_link_row, LV_OBJ_FLAG_HIDDEN);
    }
    if (ds_metrics_row != NULL) {
      lv_obj_clear_flag(ds_metrics_row, LV_OBJ_FLAG_HIDDEN);
    }
    if (ds_status_row != NULL) {
      lv_obj_clear_flag(ds_status_row, LV_OBJ_FLAG_HIDDEN);
    }
    if (ds_status_la != NULL) {
      lv_label_set_text(ds_status_la, EPD_STATUS_LABEL_STATUS);
    }
    if (ds_status_ra != NULL) {
      (void)snprintf(status_ra, sizeof(status_ra), "%s/%s", EPD_STATUS_JOINED,
                     epd_lora_region_str());
      lv_label_set_text(ds_status_ra, status_ra);
    }

    lora_link_stats_snapshot_t ls;
    memset(&ls, 0, sizeof(ls));
    ls.best_demod_margin = LORA_LINK_STATS_MARGIN_NONE;
    (void)lora_link_stats_get(&ls);

    const int link_mode = atomic_get(&device_status_link_mode);
    const bool rx_timeout = (link_mode == 2);
    int16_t margin = LORA_LINK_STATS_MARGIN_NONE;
    uint8_t gw = 0;

    if (!rx_timeout) {
      if (link_mode == 1) {
        margin = ls.last_demod_margin;
        gw = ls.last_nb_gateways;
      } else {
        margin = ls.best_demod_margin;
        gw = ls.best_nb_gateways;
      }
      if (ls.samples == 0) {
        margin = LORA_LINK_STATS_MARGIN_NONE;
        gw = 0;
      }
    }

    if (ds_link_la != NULL) {
      lv_label_set_text(ds_link_la, EPD_STATUS_LABEL_LINK);
    }
    if (ds_link_ra != NULL) {
      lv_label_set_text(ds_link_ra, device_status_link_tier_margin(margin));
    }
    if (ds_metrics_la != NULL) {
      if (!rx_timeout && ls.samples > 0) {
        (void)snprintf(metrics_la, sizeof(metrics_la), "%s %u",
                       EPD_STATUS_LABEL_GATEWAYS, (unsigned)gw);
        lv_label_set_text(ds_metrics_la, metrics_la);
      } else {
        lv_label_set_text(ds_metrics_la, EPD_STATUS_GATEWAYS_EMPTY);
      }
    }
    if (ds_metrics_ra != NULL) {
      if (!rx_timeout && ls.samples > 0 &&
          margin != LORA_LINK_STATS_MARGIN_NONE) {
        (void)snprintf(metrics_ra, sizeof(metrics_ra), "%s %d%s",
                       EPD_STATUS_LABEL_MARGIN, (int)margin,
                       EPD_STATUS_MARGIN_SUFFIX);
        lv_label_set_text(ds_metrics_ra, metrics_ra);
      } else {
        lv_label_set_text(ds_metrics_ra, EPD_STATUS_MARGIN_EMPTY);
      }
    }

    LOG_DBG("epd device_status %s %s m=%d gw=%u n=%u mode=%d",
            device_status_link_tier_margin(margin), status_ra, (int)margin,
            (unsigned)gw, (unsigned)ls.samples, link_mode);
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
      LOG_ERR("display not ready");
      return;
    }

    /* Power on EPD. Skip the blanking_off dance (which issues a DISPLAY_UPDATE
    * quick-resume that can trip into cold-start when the panel is in deep sleep
    * or lost rail) when the controller reports it is still powered — i.e. we
    * just came off a partial refresh and the rail keep-alive kept it warm. */
    if (ssd1683_is_powered_on(display)) {
      LOG_DBG("panel already powered, skip blanking_off");
      ret = 0;
    } else {
      const int64_t t_blank = k_uptime_get();
      for (int attempt = 0; attempt < 3; attempt++) {
        ret = display_blanking_off(display);
        if (ret == 0) {
          break;
        }
        LOG_WRN("blanking_off failed: %d (attempt %d/3)", ret, attempt + 1);
        k_msleep(2000);
      }
      if (ret < 0) {
        LOG_ERR("blanking_off failed after retries: %d", ret);
        return;
      }
      LOG_DBG("epd blanking_off %lld ms",
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
    if (screen_device_status) {
      lv_obj_add_flag(screen_device_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (screen_dl_custom) {
      lv_obj_add_flag(screen_dl_custom, LV_OBJ_FLAG_HIDDEN);
    }

    /* Show the requested screen */
    switch (type) {
    case JOB_SHOW_LOGO:
      LOG_STATE("epd screen LOGO");
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
      LOG_STATE("epd screen LAST_CLEANED %s", ts);
      if (last_cleaned_label) {
        lv_label_set_text(last_cleaned_label, ts);
      }
      scr_to_show = screen_last_cleaned;
      break;
    }
    case JOB_SHOW_THANKS:
      LOG_STATE("epd screen THANKS");
      scr_to_show = screen_thanks;
      break;
    case JOB_SHOW_CLEANING:
      LOG_STATE("epd screen CLEANING");
      scr_to_show = screen_cleaning;
      break;
    case JOB_SHOW_CONNECTING:
      LOG_STATE("epd screen CONNECTING");
      scr_to_show = screen_connecting;
      break;
    case JOB_SHOW_DEVICE_STATUS:
      LOG_STATE("epd screen DEVICE_STATUS");
      scr_to_show = screen_device_status;
      break;
    case JOB_SHOW_DL_CUSTOM_MESSAGE:
      LOG_DBG("epd show DL_CUSTOM");
      if (dl_custom_msg_label != NULL) {
        k_mutex_lock(&dl_custom_msg_mutex, K_FOREVER);
        lv_label_set_text(dl_custom_msg_label, dl_custom_msg_buf);
        k_mutex_unlock(&dl_custom_msg_mutex);

      }
      scr_to_show = screen_dl_custom;

      break;

    case JOB_FULL_REFRESH:
      LOG_DBG("epd full_refresh");
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
      case DISPLAY_SCREEN_DEVICE_STATUS:
        scr_to_show = screen_device_status;
        break;
      case DISPLAY_SCREEN_DL_CUSTOM:
        scr_to_show = screen_dl_custom;
        break;
      default:
        break;
      }
      break;
    default:
      break;
    }

    if (scr_to_show == screen_device_status) {
      refresh_device_status_dynamic();
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
    case JOB_SHOW_CONNECTING:
      if (atomic_get(&connecting_sync_waiting) != 0) {
        atomic_set(&connecting_sync_waiting, 0);
        k_sem_give(&connecting_done_sem);
      }
      break;
    case JOB_SHOW_CLEANING:
      if (atomic_get(&cleaning_sync_waiting) != 0) {
        atomic_set(&cleaning_sync_waiting, 0);
        k_sem_give(&cleaning_done_sem);
      }
      break;
    case JOB_SHOW_DEVICE_STATUS:
      if (atomic_get(&device_status_sync_waiting) != 0) {
        atomic_set(&device_status_sync_waiting, 0);
        k_sem_give(&device_status_done_sem);
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
      LOG_ERR("display device not ready");
      display_signal_sync_aborted(job.type);
      LOG_WRN("released blocked sync waiter(s) for job type=%u",
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
          LOG_DBG("epd cleaning_revert epoch=%u", epoch);
        } else {
          (void)last_cleaned_store_get(&epoch);
          LOG_WRN("epd cleaning_revert no_rtc");
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
        LOG_DBG("epd cleaning_tmr %umin", EPD_CLEANING_REVERT_MINUTES);
      } else {
        LOG_DBG("Cleaning screen reshown, timer kept running");
      }
      break;
    }
    case JOB_SHOW_CONNECTING:
      k_timer_stop(&dl_custom_revert_timer);
      current_screen = DISPLAY_SCREEN_CONNECTING;
      do_render(display, JOB_SHOW_CONNECTING, 0);
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

      LOG_DBG("epd dl_custom hold_min=%u", (unsigned)hold);

      break;

    }

    case JOB_SHOW_DEVICE_STATUS: {
      /* Commission boot: full refresh when coming from logo/connecting. */
      k_timer_stop(&dl_custom_revert_timer);
      bool need_full_refresh = (current_screen == DISPLAY_SCREEN_LOGO ||
                                current_screen == DISPLAY_SCREEN_CONNECTING ||
                                current_screen == DISPLAY_SCREEN_DL_CUSTOM);
      if (need_full_refresh) {
        ssd1683_set_fast_update(display, false);
      }
      current_screen = DISPLAY_SCREEN_DEVICE_STATUS;
      do_render(display, JOB_SHOW_DEVICE_STATUS, 0);
      if (need_full_refresh) {
        ssd1683_set_fast_update(display, true);
      }
      break;
    }
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
    LOG_DBG("flush done for job type=%u", job.type);

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
    if (job.type == JOB_SHOW_CONNECTING &&
        atomic_get(&connecting_sync_waiting)) {
      atomic_set(&connecting_sync_waiting, 0);
      k_sem_give(&connecting_done_sem);
    }
    if (job.type == JOB_SHOW_CLEANING &&
        atomic_get(&cleaning_sync_waiting)) {
      atomic_set(&cleaning_sync_waiting, 0);
      k_sem_give(&cleaning_done_sem);
    }
    if (job.type == JOB_SHOW_DEVICE_STATUS &&
        atomic_get(&device_status_sync_waiting)) {
      atomic_set(&device_status_sync_waiting, 0);
      k_sem_give(&device_status_done_sem);
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
      LOG_WRN("job queue full, drop type=%u", type);
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
    * missing downlinks. LOGO, CONNECTING, DEVICE_STATUS: 0 delay (boot/join/staff
    * UI; no fresh uplink on that path). THANKS: DISPLAY_THANKS_DELAY_MS (button
    * triggers uplink). Others: DISPLAY_WORK_DELAY_MS. */
    uint32_t delay_ms;
    if (type == JOB_SHOW_LOGO || type == JOB_SHOW_CONNECTING ||
        type == JOB_SHOW_DEVICE_STATUS) {
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
      LOG_ERR("display device not ready");
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
        LOG_ERR("failed to create LVGL display");
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
      LOG_ERR("Buffer too small: have=%d need=%d", sizeof(lvgl_buf1),
              full_buf_size);
      return -ENOMEM;
    }

    LOG_DBG("epd buf bytes=%zu stride=%u", sizeof(lvgl_buf1),
            (unsigned)stride_bytes);
    lv_display_set_buffers_with_stride(lvgl_display, lvgl_buf1, lvgl_buf2,
                                      full_buf_size, stride_bytes,
                                      LV_DISPLAY_RENDER_MODE_DIRECT);

    /* Ensure this display is the default before creating screens */
    lv_display_set_default(lvgl_display);

    /* Create all screens (they will be created on the default display) */
    create_lvgl_screens();

    LOG_STATE("epd init ok (+LVGL)");
  #else
    LOG_STATE("epd init ok (off)");
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
      LOG_WRN("logo sync: job queue full");
      return;
    }
    atomic_set(&logo_sync_waiting, 1);
    (void)k_work_schedule(&display_work, K_MSEC(0));
    if (k_sem_take(&logo_done_sem, K_MSEC(30000)) != 0) {
      LOG_WRN("logo sync timeout");
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
        LOG_WRN("SPI idle wait timeout (%u ms)", (unsigned)timeout_ms);
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
      LOG_WRN("last_cleaned sync: job queue full");
      return;
    }
    atomic_set(&last_cleaned_sync_waiting, 1);
    (void)k_work_schedule(&display_work, K_MSEC(0));
    if (k_sem_take(&last_cleaned_done_sem, K_MSEC(10000)) != 0) {
      LOG_WRN("last_cleaned sync timeout");
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
      LOG_WRN("thanks sync timeout; clearing vote UI busy");
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
      LOG_WRN("cleaning sync: job queue full");
      return;
    }
    atomic_set(&cleaning_sync_waiting, 1);
    (void)k_work_schedule(&display_work, K_MSEC(0));
    if (k_sem_take(&cleaning_done_sem, K_MSEC(10000)) != 0) {
      LOG_WRN("cleaning sync timeout");
    }
    atomic_set(&cleaning_sync_waiting, 0);
  #endif
  }

  void display_show_connecting(void) {
  #if EPD_ENABLED
    enqueue_job(JOB_SHOW_CONNECTING, 0);
  #endif
  }

  void display_show_connecting_sync(void) {
  #if EPD_ENABLED
    struct display_job job = {.type = JOB_SHOW_CONNECTING, .epoch = 0};
    if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
      LOG_WRN("connecting sync: job queue full");
      return;
    }
    atomic_set(&connecting_sync_waiting, 1);
    (void)k_work_schedule(&display_work, K_MSEC(0));
    if (k_sem_take(&connecting_done_sem, K_MSEC(30000)) != 0) {
      LOG_WRN("connecting sync timeout");
    }
    atomic_set(&connecting_sync_waiting, 0);
  #endif
  }

  void display_show_device_status(void) {
  #if EPD_ENABLED
    enqueue_job(JOB_SHOW_DEVICE_STATUS, 0);
  #endif
  }

  void display_show_device_status_sync(void) {
  #if EPD_ENABLED
    struct display_job job = {.type = JOB_SHOW_DEVICE_STATUS, .epoch = 0};
    if (k_msgq_put(&display_jobq, &job, K_NO_WAIT) != 0) {
      LOG_WRN("device_status sync: job queue full");
      return;
    }
    atomic_set(&device_status_sync_waiting, 1);
    (void)k_work_schedule(&display_work, K_MSEC(0));
    if (k_sem_take(&device_status_done_sem, K_MSEC(30000)) != 0) {
      LOG_WRN("device_status sync timeout");
    }
    atomic_set(&device_status_sync_waiting, 0);
  #endif
  }

  void display_show_device_status_for_user_sync(void) {
  #if EPD_ENABLED
    int link_mode = 0;

    if (lora_is_joined()) {
      link_mode = lora_probe_link_check_sync(DEVICE_INFO_LINK_PROBE_TIMEOUT_MS)
                      ? 1
                      : 2;
    }
    atomic_set(&device_status_link_mode, link_mode);
    display_show_device_status_sync();
    atomic_set(&device_status_link_mode, 0);
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
