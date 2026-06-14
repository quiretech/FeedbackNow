/*
 * LoRaWAN time sync -> external RTC
 *
 * Uses DeviceTimeReq/DeviceTimeAns via Zephyr LoRaWAN APIs:
 * - lorawan_request_device_time()
 * - lorawan_device_time_get()
 *
 * On success, converts GPS seconds -> Unix epoch seconds and programs the RTC.
 */

#include "time_sync.h"
#include "log_fmt.h"

#include "display_manager.h"
#include "lora_app.h"
#include "rail_manager.h"
#include "rtc.h"
#include "smf_system_mode.h"
#include "sys_config.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>

LOG_MODULE_REGISTER(time_sync, CONFIG_LOG_DEFAULT_LEVEL);

static void time_sync_apply_work_handler(struct k_work *work);
static void time_sync_timeout_work_handler(struct k_work *work);

/* Static definition so handler is never re-inited; avoids use-after-init races.
 */
K_WORK_DEFINE(time_sync_apply_work, time_sync_apply_work_handler);
K_WORK_DELAYABLE_DEFINE(time_sync_timeout_work, time_sync_timeout_work_handler);

static atomic_t time_sync_inflight = ATOMIC_INIT(0);
static atomic_t time_sync_last_result = ATOMIC_INIT(-EAGAIN);
K_SEM_DEFINE(time_sync_done_sem, 0, 1);

static void time_sync_post_done_event(int result) {
  (void)smf_post_event(SMF_EVT_TIME_SYNC_DONE, (result == 0 ? 0 : 1),
                       k_uptime_get());
}

static void time_sync_notify_done(int result) {
  time_sync_post_done_event(result);
}

static int gps_to_unix_epoch(uint32_t gps_s, uint32_t *out_unix_s) {
  if (out_unix_s == NULL) {
    return -EINVAL;
  }

  /* gps_s is GPS seconds (no leap seconds). UTC = GPS - (GPS-UTC offset). */
  uint32_t utc_s = 0;
  if (gps_s < (uint32_t)LORAWAN_GPS_UTC_LEAP_SECONDS) {
    return -EINVAL;
  }
  utc_s = gps_s - (uint32_t)LORAWAN_GPS_UTC_LEAP_SECONDS;

  /* Convert GPS-epoch-based seconds to Unix-epoch-based seconds */
  *out_unix_s = utc_s + GPS_TO_UNIX_EPOCH_OFFSET;
  return 0;
}

static int time_sync_apply_from_stack(void) {
  uint32_t gps_time = 0;
  int ret = lorawan_device_time_get(&gps_time);
  if (ret != 0) {
    return ret;
  }

  LOG_DBG("DeviceTimeAns (gps_seconds=%u)", gps_time);

  uint32_t epoch_s = 0;
  ret = gps_to_unix_epoch(gps_time, &epoch_s);
  if (ret != 0) {
    LOG_ERR("Time conversion failed (gps=%u): %d", gps_time, ret);
    return ret;
  }

  rail_manager_request_3v3a();
  ret = rtc_set_epoch_seconds(epoch_s);
  if (ret != 0) {
    LOG_WRN("RTC update skipped/failed (epoch=%u): %d", epoch_s, ret);
  } else {
    LOG_EVT("RTC synced gps=%u epoch=%u", gps_time, epoch_s);
  }
  rail_manager_release_3v3a();

  return ret;
}

static void time_sync_apply_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  if (atomic_get(&time_sync_inflight) == 0) {
    return;
  }
  int ret = time_sync_apply_from_stack();
  if (!atomic_cas(&time_sync_inflight, 1, 0)) {
    /* time_sync_abort_on_link_lost() won the teardown race */
    return;
  }
  atomic_set(&time_sync_last_result, ret);
  (void)k_work_cancel_delayable(&time_sync_timeout_work);
  k_sem_give(&time_sync_done_sem);
  time_sync_notify_done(ret);
}

static void time_sync_timeout_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  if (atomic_get(&time_sync_inflight) == 0) {
    return;
  }
  LOG_WRN("DeviceTimeAns timeout (%u ms)", (unsigned)TIME_SYNC_ANS_TIMEOUT_MS);

  if (!atomic_cas(&time_sync_inflight, 1, 0)) {
    return;
  }
  atomic_set(&time_sync_last_result, -ETIMEDOUT);
  k_sem_give(&time_sync_done_sem);
  time_sync_notify_done(-ETIMEDOUT);
}

void time_sync_request_and_update_rtc(void) {
  if (!lora_is_joined()) {
    LOG_DBG("Time sync skipped: not joined");
    return;
  }

  /* Coalesce concurrent requests */
  if (atomic_cas(&time_sync_inflight, 0, 1) == false) {
    LOG_DBG("Time sync request ignored: already in progress");
    return;
  }

  atomic_set(&time_sync_last_result, -EINPROGRESS);
  k_sem_reset(&time_sync_done_sem);

  LOG_DBG("DeviceTimeReq (network time)");

  /* Log current RTC time before attempting network sync */
  uint32_t rtc_epoch_before = 0;
  rail_manager_request_3v3a();
  int rret = rtc_get_epoch_seconds(&rtc_epoch_before);
  rail_manager_release_3v3a();
  if (rret == 0) {
    LOG_DBG("RTC epoch before=%u", rtc_epoch_before);
  } else {
    LOG_DBG("RTC epoch unreadable: %d", rret);
  }

  /* Log current LoRaWAN time state (if already available) */
  uint32_t gps_time_now = 0;
  int tret = lorawan_device_time_get(&gps_time_now);
  if (tret == 0) {
    LOG_DBG("stack time cached gps_seconds=%u", gps_time_now);
  } else {
    LOG_DBG("stack time unavailable (ret=%d)", tret);
  }

  int ret = -EBUSY;
  for (int attempt = 0; attempt < 5; attempt++) {
#if EPD_ENABLED
    display_wait_until_spi_idle(10000);
#endif
    ret = lorawan_request_device_time(true);
    if (ret == 0) {
      break;
    }
    if (ret != -EBUSY) {
      break;
    }
    LOG_DBG("DeviceTimeReq busy (attempt %d/5)", attempt + 1);
    k_msleep(500);
  }
  if (ret != 0) {
    LOG_WRN("lorawan_request_device_time failed: %d", ret);
    if (!atomic_cas(&time_sync_inflight, 1, 0)) {
      return;
    }
    atomic_set(&time_sync_last_result, ret);
    k_sem_give(&time_sync_done_sem);
    time_sync_notify_done(ret);
    return;
  }

  LOG_DBG("DeviceTimeReq queued (wait Ans)");
  (void)k_work_schedule(&time_sync_timeout_work, K_MSEC(TIME_SYNC_ANS_TIMEOUT_MS));
}

void time_sync_on_lorawan_time_updated(void) {
  if (atomic_get(&time_sync_inflight) == 0) {
    return;
  }
  (void)k_work_submit(&time_sync_apply_work);
}

int time_sync_wait(k_timeout_t timeout) {
  int ret = k_sem_take(&time_sync_done_sem, timeout);
  if (ret != 0) {
    return ret;
  }
  return (int)atomic_get(&time_sync_last_result);
}

void time_sync_abort_on_link_lost(void) {
  (void)k_work_cancel_delayable(&time_sync_timeout_work);
  (void)k_work_cancel(&time_sync_apply_work);
  lora_cancel_scheduled_burst_time_sync();

  if (!atomic_cas(&time_sync_inflight, 1, 0)) {
    return;
  }

  atomic_set(&time_sync_last_result, -ENOTCONN);
  k_sem_give(&time_sync_done_sem);
  time_sync_post_done_event(-ENOTCONN);
}
