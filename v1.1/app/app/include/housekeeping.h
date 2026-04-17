/**
 * Housekeeping / heartbeat (FRD 4.9).
 *
 * Periodic thread posts SMF_EVT_HOUSEKEEPING_TICK; SMF submits work that runs
 * housekeeping_run() (battery uplink + link check + time sync + counter sync +
 * EVT 0x13 state snapshot on FPORT_HOUSEKEEPING). Downlink DL_CMD_STATUS_REQ
 * calls housekeeping_run() directly
 * from the SMF thread for on-demand telemetry. lorawan_* stays on the LoRa
 * thread via command/uplink queues.
 */
#ifndef HOUSEKEEPING_H
#define HOUSEKEEPING_H

#include <zephyr/kernel.h>

/**
 * Start the housekeeping thread. Call once after LoRa and SMF are running.
 * Thread sleeps HOUSEKEEPING_INTERVAL_SECONDS then runs tasks in a loop.
 */
int housekeeping_init(void);
int housekeeping_wait_until_ready(k_timeout_t timeout);

/** Thread ID for main to start (K_THREAD_DEFINE with delay = -1). */
extern const k_tid_t housekeeping_thread_id;

/** Heartbeat / telemetry run (rails, battery, uplink, link check, time sync, counter sync). */
void housekeeping_run(void);

/** SMF: call on SMF_EVT_TIME_SYNC_DONE if rails were held during async sync. */
void housekeeping_on_time_sync_done(void);

#endif /* HOUSEKEEPING_H */
