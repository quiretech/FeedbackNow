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
void time_sync_on_lorawan_time_updated(void);
void time_sync_retry_request(void);

/** Call when the link is torn down (stack not joined). Cancels in-flight
 * DeviceTimeReq timeout/apply work and notifies SMF so housekeeping does not
 * hold rails; does not re-enable ADR.
 */
void time_sync_abort_on_link_lost(void);

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
