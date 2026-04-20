/**
 * Rail manager: ref-count and keep-alive for power-gated rails (3.3V, 1.8V,
 * 3.3A, 3.6V). Uses power_ctrl for GPIO. Call request_* before using a rail,
 * release_* when done. 3.3A and 3.6V have keep-alive windows after last
 * release so back-to-back users skip the power-on settle.
 */
#ifndef RAIL_MANAGER_H
#define RAIL_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/** Call once after power_ctrl_init(); ref-counts start at 0. */
int rail_manager_init(void);

/** Turn off 3.3V, 3.3A, 3.6V (e.g. after boot init). 1.8V unchanged. */
void rail_manager_enter_idle(void);

void rail_manager_request_3v3(void);
void rail_manager_release_3v3(void);

void rail_manager_request_1v8(void);
void rail_manager_release_1v8(void);

/** Request 3.3A (and 3.3V). 3.3A uses keep-alive after last release. */
void rail_manager_request_3v3a(void);
void rail_manager_release_3v3a(void);

/** Request/release 3.6V (PN5180 + LoRa radio). 3.6V uses keep-alive after
 *  last release so consecutive NFC scans skip re-init. */
void rail_manager_request_3v6(void);
void rail_manager_release_3v6(void);

/**
 * Extend 3.3A keep-alive (e.g. before NFC/EPD work). Safe to call from any
 * thread. No-op if ref-count > 0.
 */
void rail_manager_keepalive_3v3a(void);

/**
 * Query whether 3.6V is physically on right now (ref-held OR keep-alive
 * pending). Callers can skip chip re-initialization when true. Safe to call
 * from any thread.
 */
bool rail_manager_is_3v6_on(void);

/**
 * Forced power cycle of 3.6V for device recovery (e.g. wedged PN5180). Must
 * be called while the caller holds a 3v6 ref — the rail goes off for off_ms,
 * then back on; the caller sleeps settle_ms before touching the chip. Leaves
 * ref-count unchanged. No-op and logs a warning if no ref is currently held.
 */
void rail_manager_pulse_3v6_recovery(uint32_t off_ms, uint32_t settle_ms);

#endif /* RAIL_MANAGER_H */
