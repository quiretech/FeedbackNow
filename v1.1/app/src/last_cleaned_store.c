/**
 * Last cleaned display store — EEPROM-backed epoch for EPD "last cleaned" screen.
 */
#include "last_cleaned_store.h"
#include "sys_config.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(last_cleaned_store, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
#define EEPROM_NODE DT_NODELABEL(eeprom0)
static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_NODE);
#else
static const struct device *eeprom_dev = NULL;
#endif

#define STORE_EMPTY_MARKER 0xFFFFFFFFU

static struct {
  bool initialized;
} ctx;

int last_cleaned_store_init(void) {
  if (ctx.initialized) {
    return 0;
  }
  ctx.initialized = true;
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    LOG_WRN("Last cleaned store: EEPROM not available");
    return -ENODEV;
  }
  LOG_DBG("Last cleaned store init OK");
  return 0;
}

bool last_cleaned_store_get(uint32_t *epoch_out) {
  if (epoch_out == NULL) {
    return false;
  }
  if (!ctx.initialized) {
    (void)last_cleaned_store_init();
  }
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return false;
  }
  uint32_t val = STORE_EMPTY_MARKER;
  int ret = eeprom_read(eeprom_dev, (off_t)EEPROM_LAST_CLEANED_OFF, &val,
                        EEPROM_LAST_CLEANED_SIZE);
  if (ret != 0) {
    LOG_WRN("Last cleaned store read failed: %d", ret);
    return false;
  }
  if (val == STORE_EMPTY_MARKER || val == 0U) {
    return false;
  }
  *epoch_out = val;
  return true;
}

int last_cleaned_store_set(uint32_t epoch) {
  if (!ctx.initialized) {
    (void)last_cleaned_store_init();
  }
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }
  int ret = eeprom_write(eeprom_dev, (off_t)EEPROM_LAST_CLEANED_OFF, &epoch,
                         EEPROM_LAST_CLEANED_SIZE);
  if (ret != 0) {
    LOG_ERR("Last cleaned store write failed: %d", ret);
    return ret;
  }
  LOG_DBG("Last cleaned store set epoch=%u", (unsigned)epoch);
  return 0;
}

int last_cleaned_store_ensure_non_empty(uint32_t current_epoch) {
  if (!ctx.initialized) {
    (void)last_cleaned_store_init();
  }
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }
  uint32_t existing = STORE_EMPTY_MARKER;
  int ret = eeprom_read(eeprom_dev, (off_t)EEPROM_LAST_CLEANED_OFF, &existing,
                        EEPROM_LAST_CLEANED_SIZE);
  if (ret != 0) {
    return ret;
  }
  if (existing != STORE_EMPTY_MARKER && existing != 0U) {
    return 0; /* already has a value */
  }
  return last_cleaned_store_set(current_epoch);
}
