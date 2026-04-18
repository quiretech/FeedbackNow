#ifndef RTC_APP_H
#define RTC_APP_H

#include <stdint.h>

/**
 * @brief Initialize RTC access (optional).
 * @return 0 on success, negative error otherwise.
 */
int rtc_app_init(void);

/**
 * @brief Read RTC time and convert to epoch seconds.
 * @param out_epoch_s (output) seconds since Unix epoch.
 * @return 0 on success, negative error otherwise.
 */
int rtc_get_epoch_seconds(uint32_t *out_epoch_s);

/**
 * @brief Program RTC from a Unix epoch timestamp (UTC).
 * @param epoch_s seconds since Unix epoch.
 * @return 0 on success, negative error otherwise.
 */
int rtc_set_epoch_seconds(uint32_t epoch_s);

/**
 * @brief Notify RTC helper that 3V3A was enabled.
 */
void rtc_notify_3v3a_enabled(void);

#endif /* RTC_APP_H */
