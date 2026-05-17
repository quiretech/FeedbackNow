/**
 * Boot info: one-shot capture of the hardware reset cause so later code can
 * decide whether this boot is a "commissioning-class" event that deserves
 * installer UI (device status screen after join on commission boot) vs. an
 * in-field fault
 * reboot that should stay silent.
 *
 * We read CONFIG_HWINFO reset cause once, very early in main(), and clear it
 * so subsequent boots start clean.
 *
 * Commissioning boots (show installer UI):
 *  - RESET_POR      (cold power-on / first boot after assembly)
 *  - RESET_PIN      (reset button held)
 *  - RESET_SOFTWARE (sys_reboot from Staff+0+1+2+3 combo, or downlink 0x07)
 *
 * Silent / fault boots (skip installer UI):
 *  - RESET_WATCHDOG
 *  - RESET_BROWNOUT
 *  - anything else (treated conservatively as silent)
 */
#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum boot_cause_kind {
  BOOT_CAUSE_UNKNOWN = 0,
  BOOT_CAUSE_POWER_ON,        /* RESET_POR */
  BOOT_CAUSE_RESET_PIN,       /* RESET_PIN */
  BOOT_CAUSE_SOFTWARE,        /* RESET_SOFTWARE (sys_reboot) */
  BOOT_CAUSE_WATCHDOG,        /* RESET_WATCHDOG */
  BOOT_CAUSE_BROWNOUT,        /* RESET_BROWNOUT */
  BOOT_CAUSE_OTHER,           /* any other bit set */
};

/**
 * Read and clear the reset cause from hwinfo. Idempotent — subsequent calls
 * return the cached value without touching hardware. Safe to call before any
 * threads are started.
 *
 * @return 0 on success, negative errno on hwinfo failure (cause stays UNKNOWN).
 */
int boot_info_init(void);

/** Get the cached boot cause kind. Returns UNKNOWN if init hasn't run. */
enum boot_cause_kind boot_info_get_cause(void);

/** Raw cause bitmask from hwinfo (OR of RESET_* bits). 0 if unknown. */
uint32_t boot_info_get_raw(void);

/**
 * True if this boot should trigger commissioning-class UI (install screen,
 * etc.). Currently: POWER_ON, RESET_PIN, or SOFTWARE reset. Watchdog/brownout
 * and anything else return false.
 */
bool boot_info_is_commission_boot(void);

/** Short human string for logs. */
const char *boot_info_cause_str(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_INFO_H */
