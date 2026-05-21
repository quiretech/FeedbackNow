/*
 * RTC access helper for demo payload timestamps.
 *
 * Uses Zephyr RTC API when an RTC node is present in the device tree.
 * If no RTC exists, rtc_app_init()/rtc_get_epoch_seconds() return -ENODEV.
 */

#include "onboarding_config.h"
#include "rtc.h"
#include "sys_config.h"

#include <errno.h>
#include <time.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/timeutil.h>

LOG_MODULE_REGISTER(rtc_app, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_EXISTS(DT_NODELABEL(pcf8523))
#define RTC_NODE DT_NODELABEL(pcf8523)
static const struct device *rtc_dev = DEVICE_DT_GET(RTC_NODE);
#else
static const struct device *rtc_dev = NULL;
#endif
static atomic_t rtc_3v3a_enabled_at_ms = ATOMIC_INIT(0);

void rtc_notify_3v3a_enabled(void) {
  atomic_set(&rtc_3v3a_enabled_at_ms, (atomic_val_t)k_uptime_get_32());
}

static void rtc_wait_after_3v3a_enable(void) {
  uint32_t t_enabled = (uint32_t)atomic_get(&rtc_3v3a_enabled_at_ms);
  if (t_enabled == 0U) {
    return;
  }
  uint32_t now = k_uptime_get_32();
  uint32_t elapsed = now - t_enabled;
  if (elapsed < RTC_3V3A_SETTLE_MS) {
    k_msleep(RTC_3V3A_SETTLE_MS - elapsed);
  }
}

static int rtc_should_set_time(const struct rtc_time *t) {
  /* rtc_time.tm_year is years since 1900 */
  int year = t->tm_year + 1900;
  if (year < RTC_VALID_YEAR_MIN) {
    return 1;
  }
  if (t->tm_mon < 0 || t->tm_mon > 11) {
    return 1;
  }
  if (t->tm_mday < 1 || t->tm_mday > 31) {
    return 1;
  }
  if (t->tm_hour < 0 || t->tm_hour > 23 || t->tm_min < 0 || t->tm_min > 59 ||
      t->tm_sec < 0 || t->tm_sec > 59) {
    return 1;
  }
  return 0;
}

static int rtc_set_time_from_config(void) {
#if DEVICE_PROVISION_UNIX_UTC != 0ULL
  /* Single source of truth after onboarding/gen_euis.py (full or
   * --stamp-provision-only). Stale until you re-run gen_euis; RTC_SET_* below
   * is only used when provision stamp is 0. */
  uint32_t epoch = (uint32_t)DEVICE_PROVISION_UNIX_UTC;
  return rtc_set_epoch_seconds(epoch);
#else
  struct rtc_time t = {0};

  t.tm_sec = RTC_SET_SECOND;
  t.tm_min = RTC_SET_MINUTE;
  t.tm_hour = RTC_SET_HOUR;
  t.tm_mday = RTC_SET_DAY;
  t.tm_mon = RTC_SET_MONTH - 1;    /* rtc_time uses 0-11 */
  t.tm_year = RTC_SET_YEAR - 1900; /* years since 1900 */
  t.tm_wday = -1;
  t.tm_yday = -1;
  t.tm_isdst = -1;
  t.tm_nsec = 0;

  int ret = rtc_set_time(rtc_dev, &t);
  if (ret != 0) {
    return ret;
  }

  LOG_INF("RTC wall set UTC %04d-%02d-%02d %02d:%02d:%02d (config)",
          RTC_SET_YEAR, RTC_SET_MONTH, RTC_SET_DAY, RTC_SET_HOUR,
          RTC_SET_MINUTE, RTC_SET_SECOND);
  return 0;
#endif
}

int rtc_app_init(void) {

  if (rtc_dev == NULL) {
    return -ENODEV;
  }
  for (int i = 0; i < RTC_INIT_RETRY_COUNT; i++) {
    if (device_is_ready(rtc_dev)) {
      break;
    }
    k_msleep(RTC_INIT_RETRY_DELAY_MS);
  }
  if (!device_is_ready(rtc_dev)) {
    return -ENODEV;
  }

#if RTC_SET_TIME_ON_BOOT
  struct rtc_time cur = {0};
  int ret = rtc_get_time(rtc_dev, &cur);
  const bool rtc_valid = (ret == 0) && !rtc_should_set_time(&cur);

  if (RTC_PRESERVE_EXISTING_ON_BOOT && rtc_valid) {
    LOG_INF("RTC preserved (%04d-%02d-%02d %02d:%02d:%02d)",
            cur.tm_year + 1900, cur.tm_mon + 1, cur.tm_mday, cur.tm_hour,
            cur.tm_min, cur.tm_sec);
  } else {
    if (ret != 0) {
      LOG_WRN("RTC read failed (%d); applying provision time", ret);
    } else if (rtc_should_set_time(&cur)) {
      LOG_WRN("RTC uninitialized; applying provision time");
    } else {
      LOG_INF("Applying provision time from sys_config");
    }
    ret = rtc_set_time_from_config();
    if (ret != 0) {
      LOG_ERR("Failed to set RTC time: %d", ret);
      return ret;
    }
  }
#endif

  return 0;
}

int rtc_get_epoch_seconds(uint32_t *out_epoch_s) {
  if (out_epoch_s == NULL) {
    return -EINVAL;
  }
  if (rtc_dev == NULL || !device_is_ready(rtc_dev)) {
    return -ENODEV;
  }

  struct rtc_time t = {0};
  int ret = -EIO;
  rtc_wait_after_3v3a_enable();
  for (int attempt = 0; attempt < RTC_GET_EPOCH_RETRIES; attempt++) {
    if (attempt > 0) {
      k_msleep(RTC_GET_EPOCH_RETRY_DELAY_MS);
    }
    ret = rtc_get_time(rtc_dev, &t);
    if (ret == 0) {
      break;
    }
    /* -EIO (-5) typical when 3.3A rail just came on (RTC on I2C not ready yet) */
    LOG_DBG("RTC read attempt %d failed: %d", attempt + 1, ret);
  }
  if (ret != 0) {
    return ret;
  }

  /* Convert rtc_time -> epoch seconds */
  struct tm tm_utc = {
      .tm_sec = t.tm_sec,
      .tm_min = t.tm_min,
      .tm_hour = t.tm_hour,
      .tm_mday = t.tm_mday,
      .tm_mon = t.tm_mon,
      .tm_year = t.tm_year,
      .tm_wday = t.tm_wday,
      .tm_yday = t.tm_yday,
      .tm_isdst = t.tm_isdst,
  };

  int64_t epoch = timeutil_timegm64(&tm_utc);
  if (epoch < 0) {
    return -EINVAL;
  }
  *out_epoch_s = (uint32_t)epoch;
  return 0;
}

int rtc_set_epoch_seconds(uint32_t epoch_s) {
  rtc_wait_after_3v3a_enable();
  if (rtc_dev == NULL || !device_is_ready(rtc_dev)) {
    return -ENODEV;
  }

  time_t tt = (time_t)epoch_s;
  struct tm tm_utc = {0};
  if (gmtime_r(&tt, &tm_utc) == NULL) {
    return -EINVAL;
  }

  LOG_DBG("RTC epoch=%u -> %04d-%02d-%02d %02d:%02d:%02d (UTC)",
          epoch_s, tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
          tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);

  struct rtc_time t = {0};
  t.tm_sec = tm_utc.tm_sec;
  t.tm_min = tm_utc.tm_min;
  t.tm_hour = tm_utc.tm_hour;
  t.tm_mday = tm_utc.tm_mday;
  t.tm_mon = tm_utc.tm_mon;
  t.tm_year = tm_utc.tm_year;
  t.tm_wday = tm_utc.tm_wday;
  t.tm_yday = tm_utc.tm_yday;
  t.tm_isdst = tm_utc.tm_isdst;
  t.tm_nsec = 0;

  int ret = rtc_set_time(rtc_dev, &t);
  if (ret == 0) {
    LOG_INF("RTC set UTC %04d-%02d-%02d %02d:%02d:%02d epoch=%u",
            tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
            tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
            (unsigned)epoch_s);
  } else {
    LOG_ERR("RTC set failed epoch=%u: %d", epoch_s, ret);
  }
  return ret;
}