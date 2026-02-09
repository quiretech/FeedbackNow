/*
 * External EEPROM bring-up probe (AT24 compatible).
 *
 * This is only a connectivity/readiness check so we can confidently move on to
 * persistent counter storage later.
 */

#include "eeprom_probe.h"

#include "log_fmt.h"

#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eeprom_probe, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
#define EEPROM_NODE DT_NODELABEL(eeprom0)
static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_NODE);
#else
static const struct device *eeprom_dev = NULL;
#endif

void eeprom_probe_log(void) {
  if (eeprom_dev == NULL) {
    LOG_WRN("EEPROM node not present in devicetree (eeprom0)");
    return;
  }
  if (!device_is_ready(eeprom_dev)) {
    LOG_WRN("EEPROM device not ready");
    return;
  }

  uint8_t buf[16] = {0};
  int ret = eeprom_read(eeprom_dev, 0, buf, sizeof(buf));
  if (ret != 0) {
    LOG_WRN("EEPROM read failed: %d", ret);
    return;
  }

  LOG_SECTION_INF("EEPROM PROBE OK");
  LOG_INF("Read first %u bytes from EEPROM @0x56:", (unsigned)sizeof(buf));
  LOG_HEXDUMP_INF(buf, sizeof(buf), "EEPROM[0x0000..]");
}


