/*
 * Housekeeping / heartbeat worker (FRD 4.9).
 *
 * k_work_delayable schedules the next daily slot (00:00 UTC + DevEUI jitter
 * in production). Daily and DL 0x04 status runs execute on the system
 * workqueue so SMF is never blocked by ADC sleep or counter-sync spacing.
 */
#include "housekeeping.h"
#include "log_fmt.h"
#include "battery_adc.h"
#include "counter_sync.h"
#include "downlink_dispatch.h"
#include "eui_keys.h"
#include "lora_app.h"
#include "payload_gen.h"
#include "rail_manager.h"
#include "rtc.h"
#include "sys_config.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(housekeeping, CONFIG_LOG_DEFAULT_LEVEL);
K_SEM_DEFINE(housekeeping_ready_sem, 0, 1);

static K_MUTEX_DEFINE(housekeeping_run_mtx);

static uint32_t hk_offset_minutes;
static bool hk_rtc_fallback;

static void hk_schedule_next(void);
static void hk_timer_handler(struct k_work *work);
static void hk_daily_handler(struct k_work *work);
static void hk_status_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(hk_timer_work, hk_timer_handler);
K_WORK_DEFINE(hk_daily_work, hk_daily_handler);
K_WORK_DEFINE(hk_status_work, hk_status_handler);

/** Compute seconds until next daily heartbeat time (00:00 UTC +
 * offset_minutes). */
static uint32_t seconds_until_next_heartbeat(uint32_t now_epoch,
                                             uint32_t offset_minutes) {
  uint32_t offset_sec = offset_minutes * 60U;
  uint32_t sec_since_midnight = now_epoch % SECONDS_PER_DAY;
  uint32_t next_run_epoch;

  if (offset_sec > sec_since_midnight) {
    next_run_epoch =
        (now_epoch / SECONDS_PER_DAY) * SECONDS_PER_DAY + offset_sec;
  } else {
    next_run_epoch =
        (now_epoch / SECONDS_PER_DAY + 1U) * SECONDS_PER_DAY + offset_sec;
  }
  return next_run_epoch - now_epoch;
}

static uint32_t hk_compute_delay_sec(void) {
#if HEARTBEAT_USE_DEVEUI_JITTER
  uint32_t now_epoch = 0;

  if (rtc_get_epoch_seconds(&now_epoch) != 0 || now_epoch == 0) {
    hk_rtc_fallback = true;
    return HOUSEKEEPING_INTERVAL_SECONDS;
  }

  hk_rtc_fallback = false;
  uint32_t sleep_sec =
      seconds_until_next_heartbeat(now_epoch, hk_offset_minutes);
  if (sleep_sec > SECONDS_PER_DAY) {
    sleep_sec = HOUSEKEEPING_INTERVAL_SECONDS;
  }
  if (sleep_sec == 0) {
    sleep_sec = 1;
  }
  return sleep_sec;
#else
  ARG_UNUSED(hk_offset_minutes);
  hk_rtc_fallback = false;
  return HOUSEKEEPING_INTERVAL_SECONDS;
#endif
}

static void hk_schedule_next(void) {
  uint32_t delay_sec = hk_compute_delay_sec();

  LOG_DBG("next HK in %u s (rtc_fallback=%d)", (unsigned)delay_sec,
          (int)hk_rtc_fallback);
  (void)k_work_schedule(&hk_timer_work, K_SECONDS(delay_sec));
}

static void hk_timer_handler(struct k_work *work) {
  ARG_UNUSED(work);

  if (lora_is_joined()) {
    (void)k_work_submit(&hk_daily_work);
  } else {
    hk_schedule_next();
  }
}

static void hk_daily_handler(struct k_work *work) {
  ARG_UNUSED(work);

  housekeeping_run();
  hk_schedule_next();
}

static void hk_status_handler(struct k_work *work) {
  ARG_UNUSED(work);

  housekeeping_run_status_request();
}

int housekeeping_init(void) {
  static const uint8_t dev_eui[] = LORAWAN_DEV_EUI;

  hk_offset_minutes =
      (uint32_t)(dev_eui[7] * 256U + dev_eui[6]) % (uint32_t)MINUTES_PER_DAY;
  LOG_DBG("HK jitter=%d offset_min=%u", HEARTBEAT_USE_DEVEUI_JITTER,
          (unsigned)hk_offset_minutes);

  k_sem_give(&housekeeping_ready_sem);
  hk_schedule_next();
  return 0;
}

int housekeeping_wait_until_ready(k_timeout_t timeout) {
  return k_sem_take(&housekeeping_ready_sem, timeout);
}

void housekeeping_submit_status_request(void) {
  (void)k_work_submit(&hk_status_work);
}

void housekeeping_reschedule_after_rtc(void) {
#if HEARTBEAT_USE_DEVEUI_JITTER
  uint32_t now_epoch = 0;

  if (!hk_rtc_fallback) {
    return;
  }
  if (rtc_get_epoch_seconds(&now_epoch) != 0 || now_epoch == 0) {
    return;
  }

  LOG_DBG("RTC valid after sync; recomputing HK slot");
  (void)k_work_cancel_delayable(&hk_timer_work);
  hk_schedule_next();
#endif
}

static void housekeeping_run_core(bool counter_sync_burst) {
  if (k_mutex_lock(&housekeeping_run_mtx, K_SECONDS(2)) != 0) {
    LOG_WRN("run skipped (lock busy)");
    return;
  }

  if (!lora_is_joined()) {
    k_mutex_unlock(&housekeeping_run_mtx);
    return;
  }

  rail_manager_request_3v3a();
  rail_manager_request_3v3();
#if HOUSEKEEPING_RAIL_ADC_SETTLE_MS > 0
  k_msleep(HOUSEKEEPING_RAIL_ADC_SETTLE_MS);
#endif

  uint32_t epoch_s = 0;
  (void)rtc_get_epoch_seconds(&epoch_s);
  if (epoch_s == 0) {
    epoch_s = (uint32_t)(k_uptime_get() / 1000U);
  }

  int32_t battery_mv = 0;
  uint32_t app_uplinks = 0U;
  if (battery_adc_read_mv(&battery_mv) == 0 && battery_mv > 0) {
    LOG_EVT("HK bat=%dmV epoch=%u", (int)battery_mv, (unsigned)epoch_s);

    uint8_t payload[PAYLOAD_LEN_BYTES];
    int pret = payload_gen_build_battery_status(
        epoch_s, (uint16_t)battery_mv, 0, 0, payload);
    if (pret == 0) {
      lora_uplink_msg_t msg_hk = (lora_uplink_msg_t){0};
      msg_hk.port = FPORT_HOUSEKEEPING;
      msg_hk.confirmed = LORA_HEARTBEAT_UPLINK_CONFIRMED;
      msg_hk.len = PAYLOAD_LEN_BYTES;
      memcpy(msg_hk.data, payload, PAYLOAD_LEN_BYTES);
      if (lora_put_event(&msg_hk, K_MSEC(500)) == 0) {
        app_uplinks++;
      }
    }
  }

#if EPD_ENABLED
  if (downlink_queue_housekeeping_state_snapshot()) {
    app_uplinks++;
  }
#endif

  if (counter_sync_burst) {
    LOG_DBG("counter sync burst (%u buttons)", (unsigned)NUM_BUTTONS);
    app_uplinks += counter_sync_run(epoch_s, LORA_COUNTER_SYNC_CONFIRMED);
  }

  lora_schedule_time_sync_after_app_uplinks(app_uplinks);

  rail_manager_release_3v3();
  rail_manager_release_3v3a();

  k_mutex_unlock(&housekeeping_run_mtx);
}

void housekeeping_run(void) { housekeeping_run_core(false); }

void housekeeping_run_status_request(void) { housekeeping_run_core(true); }
