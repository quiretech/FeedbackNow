/**
 * Boot info: cache + classify hwinfo reset cause once on boot.
 *
 * We treat cause bits with a strict priority (fault > commissioning) so that,
 * e.g., a watchdog reset that also leaves PIN set (unusual on nRF but possible
 * on other SoCs) is classified as watchdog and stays silent.
 */
#include "boot_info.h"

#include <errno.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(boot_info, CONFIG_LOG_DEFAULT_LEVEL);

static bool cached;
static uint32_t cause_raw;
static enum boot_cause_kind cause_kind = BOOT_CAUSE_UNKNOWN;

static enum boot_cause_kind classify(uint32_t raw) {
  /* Fault / runtime causes first — any of these means NOT commissioning. */
  if (raw & RESET_WATCHDOG) {
    return BOOT_CAUSE_WATCHDOG;
  }
  if (raw & RESET_BROWNOUT) {
    return BOOT_CAUSE_BROWNOUT;
  }
  /* Commissioning-class causes. POR is the common "fresh battery" case. */
  if (raw & RESET_POR) {
    return BOOT_CAUSE_POWER_ON;
  }
  if (raw & RESET_SOFTWARE) {
    return BOOT_CAUSE_SOFTWARE;
  }
  if (raw & RESET_PIN) {
    return BOOT_CAUSE_RESET_PIN;
  }
  if (raw == 0U) {
    return BOOT_CAUSE_UNKNOWN;
  }
  return BOOT_CAUSE_OTHER;
}

int boot_info_init(void) {
  if (cached) {
    return 0;
  }

  uint32_t raw = 0U;
  int ret = hwinfo_get_reset_cause(&raw);
  if (ret == -ENOSYS) {
    /* Driver exists but feature unsupported for this SoC (shouldn't happen on
     * nRF52 — but be graceful if we ever port). */
    LOG_WRN("hwinfo_get_reset_cause unsupported (-ENOSYS)");
    raw = 0U;
  } else if (ret < 0) {
    LOG_WRN("hwinfo_get_reset_cause failed: %d", ret);
    raw = 0U;
  } else {
    /* Best-effort clear so the next boot doesn't see stale bits from this
     * cycle. Failure is non-fatal; cached value is still reported. */
    int clr = hwinfo_clear_reset_cause();
    if (clr < 0 && clr != -ENOSYS) {
      LOG_DBG("hwinfo_clear_reset_cause ret=%d (non-fatal)", clr);
    }
  }

  cause_raw = raw;
  cause_kind = classify(raw);
  cached = true;
  LOG_INF("cause raw=0x%08x kind=%s commission=%d", (unsigned)cause_raw,
          boot_info_cause_str(), (int)boot_info_is_commission_boot());
  return ret == -ENOSYS ? 0 : ret;
}

enum boot_cause_kind boot_info_get_cause(void) {
  return cause_kind;
}

uint32_t boot_info_get_raw(void) {
  return cause_raw;
}

bool boot_info_is_commission_boot(void) {
  switch (cause_kind) {
  case BOOT_CAUSE_POWER_ON:
  case BOOT_CAUSE_RESET_PIN:
  case BOOT_CAUSE_SOFTWARE:
    return true;
  default:
    return false;
  }
}

const char *boot_info_cause_str(void) {
  switch (cause_kind) {
  case BOOT_CAUSE_POWER_ON:
    return "POWER_ON";
  case BOOT_CAUSE_RESET_PIN:
    return "RESET_PIN";
  case BOOT_CAUSE_SOFTWARE:
    return "SOFTWARE";
  case BOOT_CAUSE_WATCHDOG:
    return "WATCHDOG";
  case BOOT_CAUSE_BROWNOUT:
    return "BROWNOUT";
  case BOOT_CAUSE_OTHER:
    return "OTHER";
  case BOOT_CAUSE_UNKNOWN:
  default:
    return "UNKNOWN";
  }
}
