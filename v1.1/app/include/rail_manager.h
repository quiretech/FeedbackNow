/**
 * Rail manager: ref-count and keep-alive for power-gated rails (3.3V, 1.8V,
 * 3.3A, 3.6V). Uses power_ctrl for GPIO. Call request_* before using a rail,
 * release_* when done. 3.3A has a keep-alive (e.g. 6s) after last release so
 * delayed EEPROM flush can complete.
 */
#ifndef RAIL_MANAGER_H
#define RAIL_MANAGER_H

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

void rail_manager_request_3v6(void);
void rail_manager_release_3v6(void);

/**
 * Extend 3.3A keep-alive (e.g. before NFC/EPD work). Safe to call from any
 * thread. No-op if ref-count > 0.
 */
void rail_manager_keepalive_3v3a(void);

#endif /* RAIL_MANAGER_H */
