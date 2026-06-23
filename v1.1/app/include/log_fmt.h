#ifndef LOG_FMT_H
#define LOG_FMT_H

/*
 * Logging policy (CONFIG_LOG_DEFAULT_LEVEL in prj.conf).
 *
 * Level | Use
 * ------+----------------------------------------------------------
 * ERR   | Hard failure; action failed or data lost.
 * WRN   | Degraded/recoverable (join retry, queue full, session lost).
 * INF   | LOG_STATE() subsystem transitions + LOG_EVT() field telemetry.
 * DBG   | Detail (GPIO masks, EPD timings, MAC steps) — compiled out when
 *       | CONFIG_LOG_DEFAULT_LEVEL < 4.
 *
 * RTT field visibility (default prj.conf level 3):
 *   LOG_STATE — SMF modes, combos, EPD screen, LoRa join steps, NFC scan.
 *   LOG_EVT   — DevEUI, joined, uplinks, downlinks, HK battery, RTC sync.
 *
 * Full trace: set CONFIG_LOG_DEFAULT_LEVEL=4 (DBG) in prj.conf overlay.
 */

#include <zephyr/logging/log.h>

/** Subsystem / mode transition — visible at CONFIG_LOG_DEFAULT_LEVEL=3. */
#define LOG_STATE(...) LOG_INF(__VA_ARGS__)

/** Curated one-line field telemetry — visible at CONFIG_LOG_DEFAULT_LEVEL=3. */
#define LOG_EVT(...) LOG_INF(__VA_ARGS__)

#define LOG_SECTION_INF(title) LOG_INF("== %s ==", title)
#define LOG_SECTION_WRN(title) LOG_WRN("== %s ==", title)
#define LOG_SECTION_ERR(title) LOG_ERR("== %s ==", title)

#endif /* LOG_FMT_H */
