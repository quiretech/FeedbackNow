#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <zephyr/kernel.h>

/**
 * @brief Request LoRaWAN network time (DeviceTimeReq) and, once available,
 *        program the external RTC (if present).
 *
 * Safe to call multiple times; it will coalesce work.
 */
void time_sync_request_and_update_rtc(void);

/**
 * @brief Wait for the current time sync attempt to complete.
 *
 * Returns:
 * - 0 on success (DeviceTimeAns received AND RTC successfully programmed)
 * - -ETIMEDOUT if the wait timed out
 * - negative errno if the time sync completed but failed
 */
int time_sync_wait(k_timeout_t timeout);

#endif /* TIME_SYNC_H */
