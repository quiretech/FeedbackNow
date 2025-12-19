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

#endif /* RTC_APP_H */
