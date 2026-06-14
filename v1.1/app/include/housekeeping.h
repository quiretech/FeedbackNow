/**
 * Housekeeping / heartbeat (FRD 4.9).
 *
 * k_work_delayable schedules the next daily slot. Daily HK and DL 0x04 status
 * requests run on the system workqueue (battery [+ snapshot on Plus], then
 * deferred LinkCheck and DeviceTimeReq). lorawan_* stays on the LoRa thread
 * via command/uplink queues.
 */
#ifndef HOUSEKEEPING_H
#define HOUSEKEEPING_H

#include <zephyr/kernel.h>

/** Schedule first HK slot. Call once after LoRa is running. */
int housekeeping_init(void);
int housekeeping_wait_until_ready(k_timeout_t timeout);

/** Daily heartbeat: battery (+ 0x13 snapshot on Plus), then LinkCheck + time sync. */
void housekeeping_run(void);

/** DL 0x04 status request: HK telemetry + 6× EVT_COUNTER_SYNC, then LinkCheck + time sync. */
void housekeeping_run_status_request(void);

/** Queue DL 0x04 status run on system workqueue (non-blocking for SMF). */
void housekeeping_submit_status_request(void);

/** After DeviceTime sets RTC, switch from uptime fallback to UTC + DevEUI slot. */
void housekeeping_reschedule_after_rtc(void);

#endif /* HOUSEKEEPING_H */
