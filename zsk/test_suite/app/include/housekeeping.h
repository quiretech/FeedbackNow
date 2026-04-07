/**
 * Housekeeping / heartbeat worker (FRD 4.9).
 *
 * Single periodic worker that runs a task list each tick. Does not call
 * lorawan_* APIs; it requests actions via LoRa command queue (time sync)
 * or uplink queue (heartbeat payload). SMF remains the orchestrator;
 * housekeeping is an autonomous worker that posts no events to SMF for now.
 *
 * Current tasks: RTC sync (request LoRa thread to run DeviceTimeReq).
 * Future: link check request, battery ADC + heartbeat uplink (Event 0x04),
 * low-battery uplink (Event 0x05).
 */
#ifndef HOUSEKEEPING_H
#define HOUSEKEEPING_H

#include <zephyr/kernel.h>

/**
 * Start the housekeeping thread. Call once after LoRa and SMF are running.
 * Thread sleeps HOUSEKEEPING_INTERVAL_SECONDS then runs tasks in a loop.
 */
int housekeeping_init(void);

/** Thread ID for main to start (K_THREAD_DEFINE with delay = -1). */
extern const k_tid_t housekeeping_thread_id;

#endif /* HOUSEKEEPING_H */
