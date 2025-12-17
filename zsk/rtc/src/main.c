/*
 * PCF8523 RTC Time Setting Example
 *
 * This application uses the official Zephyr PCF8523 driver to set and read
 * time. Modify the TIME_TO_SET structure below to set your desired time.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "power_ctrl.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

/* Get the RTC device from device tree */
static const struct device *rtc_dev = DEVICE_DT_GET(DT_NODELABEL(pcf8523));

/*
 * ============================================
 * CONFIGURE YOUR TIME HERE
 * ============================================
 * Modify these values to set the desired time on your RTC.
 *
 * Note: tm_year is years since 1900 (e.g., 2025 = 125)
 *       tm_mon is 0-11 (e.g., December = 11)
 *       tm_wday is 0-6 (Sunday = 0, Monday = 1, etc.)
 */
#define SET_YEAR 2025 /* Full year (e.g., 2025) */
#define SET_MONTH 12  /* Month 1-12 */
#define SET_DAY 17    /* Day of month 1-31 */
#define SET_HOUR 13   /* Hour 0-23 (24-hour format) */
#define SET_MINUTE 39 /* Minute 0-59 */
#define SET_SECOND 0  /* Second 0-59 */
#define SET_WEEKDAY 3 /* 0=Sunday, 1=Monday, ... 6=Saturday */

/* Helper function to print RTC time */
static void print_rtc_time(const struct rtc_time *tm) {
  static const char *weekdays[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                   "Thursday", "Friday", "Saturday"};

  printk("Time: %02d:%02d:%02d\n", tm->tm_hour, tm->tm_min, tm->tm_sec);
  printk("Date: %04d-%02d-%02d (%s)\n", tm->tm_year + 1900, tm->tm_mon + 1,
         tm->tm_mday,
         (tm->tm_wday >= 0 && tm->tm_wday <= 6) ? weekdays[tm->tm_wday]
                                                : "Unknown");
}

/* Set time on the RTC */
static int set_rtc_time(void) {
  struct rtc_time time_to_set = {
      .tm_sec = SET_SECOND,
      .tm_min = SET_MINUTE,
      .tm_hour = SET_HOUR,
      .tm_mday = SET_DAY,
      .tm_mon = SET_MONTH - 1,    /* Convert to 0-11 range */
      .tm_year = SET_YEAR - 1900, /* Convert to years since 1900 */
      .tm_wday = SET_WEEKDAY,
      .tm_yday = -1,  /* Not used by PCF8523 */
      .tm_isdst = -1, /* Not used */
      .tm_nsec = 0,
  };
  int ret;

  printk("\n--- Setting RTC Time ---\n");
  printk("Setting to: %04d-%02d-%02d %02d:%02d:%02d\n", SET_YEAR, SET_MONTH,
         SET_DAY, SET_HOUR, SET_MINUTE, SET_SECOND);

  ret = rtc_set_time(rtc_dev, &time_to_set);
  if (ret != 0) {
    LOG_ERR("Failed to set RTC time: %d", ret);
    return ret;
  }

  printk("RTC time set successfully!\n");
  return 0;
}

/* Read and display time from the RTC */
static int read_rtc_time(void) {
  struct rtc_time current_time;
  int ret;

  ret = rtc_get_time(rtc_dev, &current_time);
  if (ret != 0) {
    LOG_ERR("Failed to read RTC time: %d", ret);
    return ret;
  }

  print_rtc_time(&current_time);
  return 0;
}

int main(void) {
  int ret;

  printk("\n========================================\n");
  printk("PCF8523 RTC Driver Example\n");
  printk("========================================\n\n");

  /* Initialize and enable all power rails first */
  printk("--- Initializing Power Rails ---\n");
  ret = power_ctrl_init();
  if (ret != 0) {
    LOG_ERR("Failed to initialize power control: %d", ret);
    return ret;
  }

  /* Enable all power rails in sequence with delays for stability */
  printk("Enabling EN_3V3...\n");
  ret = power_ctrl_set(POWER_EN_3V3, true);
  if (ret != 0) {
    LOG_ERR("Failed to enable 3V3 rail: %d", ret);
    return ret;
  }
  k_msleep(10);

  printk("Enabling EN_1V8...\n");
  ret = power_ctrl_set(POWER_EN_1V8, true);
  if (ret != 0) {
    LOG_ERR("Failed to enable 1V8 rail: %d", ret);
    return ret;
  }
  k_msleep(10);

  printk("Enabling EN_3V3A...\n");
  ret = power_ctrl_set(POWER_EN_3V3A, true);
  if (ret != 0) {
    LOG_ERR("Failed to enable 3V3A rail: %d", ret);
    return ret;
  }
  k_msleep(10);

  printk("Enabling EN_3V6...\n");
  ret = power_ctrl_set(POWER_EN_3V6, true);
  if (ret != 0) {
    LOG_ERR("Failed to enable 3V6 rail: %d", ret);
    return ret;
  }
  k_msleep(50); /* Extra settle time after all rails enabled */

  printk("All power rails enabled!\n\n");

  /* Check if RTC device is ready */
  if (!device_is_ready(rtc_dev)) {
    LOG_ERR("RTC device not ready!");
    return -ENODEV;
  }

  printk("RTC device is ready.\n");

  /* Read current time before setting */
  printk("\n--- Current RTC Time (before setting) ---\n");
  ret = read_rtc_time();
  if (ret != 0) {
    printk("Could not read current time (RTC may be uninitialized)\n");
  }

  /* Set the new time */
  ret = set_rtc_time();
  if (ret != 0) {
    LOG_ERR("Failed to set time, aborting");
    return ret;
  }

  /* Small delay to ensure time is set */
  k_msleep(100);

  /* Continuously read and display time */
  printk("\n--- Reading RTC Time Continuously ---\n");
  while (1) {
    ret = read_rtc_time();
    if (ret != 0) {
      printk("Error reading time\n");
    }
    printk("---\n");
    k_msleep(1000); /* Read every second */
  }

  return 0;
}
