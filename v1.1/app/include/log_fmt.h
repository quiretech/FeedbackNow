#ifndef LOG_FMT_H
#define LOG_FMT_H

/*
 * Logging policy (CONFIG_LOG_DEFAULT_LEVEL=1 in prj.conf → ERR only on RTT).
 *
 * Level | Use
 * ------+----------------------------------------------------------
 * ERR   | Hard failure; action failed or data lost; needs attention.
 * WRN   | Degraded/recoverable (join retry, session lost, queue full,
 *       | time sync fail, invalid input). Visible at default level 2.
 * INF   | LOG_EVT() only — curated field telemetry (see below).
 * DBG   | Everything else (mode changes, EPD screens, init, joins steps).
 *
 * RTT field debug: set CONFIG_LOG_DEFAULT_LEVEL=3 (INF). You should see
 * only LOG_EVT lines plus any WRN/ERR, not EPD/SMF/join-step noise.
 *
 * LOG_EVT curated lines (INF):
 *   - Boot DevEUI (hexdump in lora_thread)
 *   - LoRaWAN joined
 *   - DL dispatch: port, len, cmd byte
 *   - DL app payload: port, len (lora_app)
 *   - HK battery: bat mV + epoch (housekeeping)
 *   - Vote uplink: btn, counter, ts (app_logic)
 *   - RTC synced from network (time_sync)
 */

#include <zephyr/logging/log.h>

/** Curated one-line telemetry for RTT at LOG_DEFAULT_LEVEL=3. */
#define LOG_EVT(...) LOG_INF(__VA_ARGS__)

#define LOG_SECTION_INF(title)  LOG_DBG("== %s ==", title)
#define LOG_SECTION_WRN(title)  LOG_WRN("== %s ==", title)
#define LOG_SECTION_ERR(title)  LOG_ERR("== %s ==", title)

#endif /* LOG_FMT_H */
