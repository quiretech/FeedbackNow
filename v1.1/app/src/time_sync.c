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
static atomic_t time_sync_retries_done = ATOMIC_INIT(0);

static void time_sync_notify_done(int result) {
  (void)smf_post_event(SMF_EVT_TIME_SYNC_DONE, (result == 0 ? 0 : 1),
                       k_uptime_get());
  /* Re-enable ADR so network manages DR; was off for DeviceTimeAns phase. */
  lora_request_enable_adr();
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

  LOG_SECTION_INF("TIME SYNC: DeviceTimeAns received");
  LOG_INF("LoRaWAN device time (GPS seconds) = %u", gps_time);

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
    LOG_INF("RTC synced from LoRaWAN time (gps=%u -> epoch=%u)", gps_time, epoch_s);
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
  atomic_set(&time_sync_inflight, 0);
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
  int retry = (int)atomic_inc(&time_sync_retries_done);
  if (retry + 1 < TIME_SYNC_MAX_RETRIES) {
    LOG_WRN("DeviceTimeAns timeout (attempt %d/%d); scheduling retry",
            retry + 1, TIME_SYNC_MAX_RETRIES);
    if (lora_cmd_put(LORA_CMD_TIME_SYNC_RETRY) == 0) {
      return;
    }
    LOG_WRN("Failed to queue DeviceTimeReq retry command");
  } else {
    LOG_WRN("DeviceTimeReq gave up after %d attempts (no DeviceTimeAns)",
            TIME_SYNC_MAX_RETRIES);
  }

  atomic_set(&time_sync_inflight, 0);
  atomic_set(&time_sync_last_result, -ETIMEDOUT);
  k_sem_give(&time_sync_done_sem);
  time_sync_notify_done(-ETIMEDOUT);
}

void time_sync_request_and_update_rtc(void) {
  /* Coalesce concurrent requests */
  if (atomic_cas(&time_sync_inflight, 0, 1) == false) {
    LOG_DBG("Time sync request ignored: already in progress");
    return;
  }

  atomic_set(&time_sync_retries_done, 0);
  atomic_set(&time_sync_last_result, -EINPROGRESS);
  k_sem_reset(&time_sync_done_sem);

  LOG_SECTION_INF("TIME SYNC: requesting network time");

  /* Log current RTC time before attempting network sync */
  uint32_t rtc_epoch_before = 0;
  rail_manager_request_3v3a();
  int rret = rtc_get_epoch_seconds(&rtc_epoch_before);
  rail_manager_release_3v3a();
  if (rret == 0) {
    LOG_INF("RTC epoch before request: %u", rtc_epoch_before);
  } else {
    LOG_WRN("RTC epoch before request unavailable: %d", rret);
  }

  /* Log current LoRaWAN time state (if already available) */
  uint32_t gps_time_now = 0;
  int tret = lorawan_device_time_get(&gps_time_now);
  if (tret == 0) {
    LOG_INF("LoRaWAN device time already available (GPS seconds) = %u",
            gps_time_now);
  } else {
    LOG_INF("LoRaWAN device time not yet available (ret=%d)", tret);
  }

  int ret = lorawan_request_device_time(true);
  if (ret != 0) {
    LOG_WRN("lorawan_request_device_time failed: %d", ret);
    atomic_set(&time_sync_inflight, 0);
    atomic_set(&time_sync_last_result, ret);
    k_sem_give(&time_sync_done_sem);
    time_sync_notify_done(ret);
    return;
  }

  LOG_INF("DeviceTimeReq sent; waiting for DeviceTimeAns callback");
  (void)k_work_schedule(&time_sync_timeout_work, K_MSEC(TIME_SYNC_ANS_TIMEOUT_MS));
}

void time_sync_on_lorawan_time_updated(void) {
  if (atomic_get(&time_sync_inflight) == 0) {
    return;
  }
  (void)k_work_submit(&time_sync_apply_work);
}

void time_sync_retry_request(void) {
  if (atomic_get(&time_sync_inflight) == 0) {
    return;
  }
  int ret = lorawan_request_device_time(true);
  if (ret != 0) {
    LOG_WRN("Retry DeviceTimeReq failed: %d", ret);
    atomic_set(&time_sync_inflight, 0);
    atomic_set(&time_sync_last_result, ret);
    k_sem_give(&time_sync_done_sem);
    time_sync_notify_done(ret);
    return;
  }
  int attempt = (int)atomic_get(&time_sync_retries_done);
  LOG_INF("DeviceTimeReq retry %d/%d sent", attempt, TIME_SYNC_MAX_RETRIES);
  (void)k_work_schedule(&time_sync_timeout_work, K_MSEC(TIME_SYNC_ANS_TIMEOUT_MS));
}

int time_sync_wait(k_timeout_t timeout) {
  int ret = k_sem_take(&time_sync_done_sem, timeout);
  if (ret != 0) {
    return ret;
  }
  return (int)atomic_get(&time_sync_last_result);
}
