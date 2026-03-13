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
#include "rtc.h"
#include "sys_config.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>

LOG_MODULE_REGISTER(time_sync, CONFIG_LOG_DEFAULT_LEVEL);

/* When LNS time sync is required, override max polls from sys_config. */
#if RTC_REQUIRE_LNS_TIME_SYNC
#undef TIME_SYNC_MAX_POLLS
#define TIME_SYNC_MAX_POLLS                                                      \
  (((RTC_TIME_SYNC_REQUIRED_TIMEOUT_SECONDS * 1000) + TIME_SYNC_POLL_INTERVAL_MS - 1) / \
   TIME_SYNC_POLL_INTERVAL_MS)
#endif

static void time_sync_work_handler(struct k_work *work);

/* Static definition so handler is never re-inited; avoids use-after-init races.
 */
K_WORK_DELAYABLE_DEFINE(time_sync_work, time_sync_work_handler);

static atomic_t time_sync_inflight = ATOMIC_INIT(0);
static int time_sync_poll_count;
static atomic_t time_sync_last_result = ATOMIC_INIT(-EAGAIN);
K_SEM_DEFINE(time_sync_done_sem, 0, 1);

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

static void time_sync_work_handler(struct k_work *work) {
  ARG_UNUSED(work);

  uint32_t gps_time = 0;
  int ret = lorawan_device_time_get(&gps_time);
  if (ret == 0) {
    LOG_SECTION_INF("TIME SYNC: DeviceTimeAns received");
    LOG_INF("LoRaWAN device time (GPS seconds) = %u", gps_time);

    uint32_t epoch_s = 0;
    ret = gps_to_unix_epoch(gps_time, &epoch_s);
    if (ret != 0) {
      LOG_ERR("Time conversion failed (gps=%u): %d", gps_time, ret);
    } else {
      LOG_INF("Converting GPS->UTC->Unix:");
      LOG_INF("  - GPS epoch->Unix epoch offset: %u s",
              GPS_TO_UNIX_EPOCH_OFFSET);
      LOG_INF("  - GPS-UTC leap seconds: %d", LORAWAN_GPS_UTC_LEAP_SECONDS);
      LOG_INF("  - Resulting Unix epoch seconds: %u", epoch_s);

      ret = rtc_set_epoch_seconds(epoch_s);
      if (ret != 0) {
        LOG_WRN("RTC update skipped/failed (epoch=%u): %d", epoch_s, ret);
      } else {
        LOG_INF("RTC synced from LoRaWAN time (gps=%u -> epoch=%u)", gps_time,
                epoch_s);

        /* Readback for extra confidence */
        uint32_t verify_epoch_s = 0;
        int vret = rtc_get_epoch_seconds(&verify_epoch_s);
        if (vret == 0) {
          LOG_INF("RTC readback epoch=%u (delta=%d s)", verify_epoch_s,
                  (int32_t)verify_epoch_s - (int32_t)epoch_s);
        } else {
          LOG_WRN("RTC readback failed: %d", vret);
        }
      }
    }

    atomic_set(&time_sync_inflight, 0);
    atomic_set(&time_sync_last_result, ret);
    k_sem_give(&time_sync_done_sem);
    return;
  }

  time_sync_poll_count++;
  if (time_sync_poll_count >= TIME_SYNC_MAX_POLLS) {
    LOG_WRN("LoRaWAN device time not available after %d polls; giving up",
            time_sync_poll_count);
    atomic_set(&time_sync_inflight, 0);
    atomic_set(&time_sync_last_result, -ETIMEDOUT);
    k_sem_give(&time_sync_done_sem);
    return;
  }

  LOG_INF("Waiting for DeviceTimeAns... (poll %d/%d, ret=%d)",
          time_sync_poll_count, TIME_SYNC_MAX_POLLS, ret);
  (void)k_work_schedule(&time_sync_work, K_MSEC(TIME_SYNC_POLL_INTERVAL_MS));
}

void time_sync_request_and_update_rtc(void) {
  /* Coalesce concurrent requests */
  if (atomic_cas(&time_sync_inflight, 0, 1) == false) {
    LOG_DBG("Time sync request ignored: already in progress");
    return;
  }

  time_sync_poll_count = 0;
  atomic_set(&time_sync_last_result, -EINPROGRESS);
  k_sem_reset(&time_sync_done_sem);

  LOG_SECTION_INF("TIME SYNC: requesting network time");

  /* Log current RTC time before attempting network sync */
  uint32_t rtc_epoch_before = 0;
  int rret = rtc_get_epoch_seconds(&rtc_epoch_before);
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
    return;
  }

  LOG_INF("DeviceTimeReq sent; will update RTC on DeviceTimeAns");
  (void)k_work_schedule(&time_sync_work, K_MSEC(TIME_SYNC_POLL_INTERVAL_MS));
}

int time_sync_wait(k_timeout_t timeout) {
  int ret = k_sem_take(&time_sync_done_sem, timeout);
  if (ret != 0) {
    return ret;
  }
  return (int)atomic_get(&time_sync_last_result);
}
