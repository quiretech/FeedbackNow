#ifndef POWER_RAIL_MGR_H
#define POWER_RAIL_MGR_H

#include <zephyr/kernel.h>

/**
 * 3V3A rail policy:
 * - LoRa requires 3V3A OFF during join/TX/RX windows.
 * - NFC and EPD require 3V3A ON while operating.
 *
 * This module arbitrates access and guarantees mutually-exclusive modes.
 */

enum power_rail_client {
  POWER_RAIL_CLIENT_LORA = 0,
  POWER_RAIL_CLIENT_NFC,
  POWER_RAIL_CLIENT_EPD,
  POWER_RAIL_CLIENT_BOOT,
  POWER_RAIL_CLIENT_SD,
  POWER_RAIL_CLIENT_COUNT,
};

int power_rail_mgr_init(void);

/**
 * @brief Dump current rail arbitration state (best-effort, for debugging).
 *
 * Prints current ON/OFF hold totals and any non-zero per-client holds.
 */
void power_rail_mgr_dump_state(void);

/**
 * Require 3V6 rail ON for the caller.
 *
 * This rail is intended to be enabled only while NFC is operating.
 * Holds are reference-counted per client; when the last hold is released,
 * the rail is turned OFF.
 */
int power_rail_mgr_require_3v6_on(enum power_rail_client client,
                                  k_timeout_t timeout);
void power_rail_mgr_release_3v6_on(enum power_rail_client client);

/**
 * Require 3V3A rail ON for the caller.
 *
 * Blocks until no OFF-requirements are active, then turns 3V3A ON (if needed)
 * and holds it ON until released.
 */
int power_rail_mgr_require_3v3a_on(enum power_rail_client client,
                                   k_timeout_t timeout);
void power_rail_mgr_release_3v3a_on(enum power_rail_client client);

/**
 * Require 3V3A rail OFF for the caller (LoRa).
 *
 * Blocks until no ON-requirements are active, then turns 3V3A OFF (if needed)
 * and holds it OFF until released.
 */
int power_rail_mgr_require_3v3a_off(enum power_rail_client client,
                                    k_timeout_t timeout);
void power_rail_mgr_release_3v3a_off(enum power_rail_client client);

#endif /* POWER_RAIL_MGR_H */
