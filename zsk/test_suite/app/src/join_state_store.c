/**
 * Persistent has_joined_once in EEPROM. Layout: magic (4B) + flag (1B) + pad.
 */
#include "join_state_store.h"
#include "sys_config.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(join_state_store, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
#define EEPROM_NODE DT_NODELABEL(eeprom0)
static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_NODE);
#else
static const struct device *eeprom_dev = NULL;
#endif

#define JOIN_STATE_MAGIC 0x4A6E3155U /* 'Jn1U' */

struct __packed join_state_record {
  uint32_t magic;
  uint8_t has_joined_once;
  uint8_t pad[EEPROM_JOIN_STATE_SIZE - 4U - 1U];
};

static struct {
  bool initialized;
  bool has_joined_once;
} ctx;

static int eeprom_read_join_state(struct join_state_record *out) {
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }
  return eeprom_read(eeprom_dev, (off_t)EEPROM_JOIN_STATE_OFF, out,
                     sizeof(struct join_state_record));
}

static int eeprom_write_join_state(const struct join_state_record *rec) {
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }
  return eeprom_write(eeprom_dev, (off_t)EEPROM_JOIN_STATE_OFF, rec,
                      sizeof(struct join_state_record));
}

int join_state_store_init(void) {
  if (ctx.initialized) {
    return 0;
  }
#if EEPROM_JOIN_STATE_CLEAR_ON_BOOT
  ctx.has_joined_once = false;
  ctx.initialized = true;
  LOG_INF("Join state: CLEAR_ON_BOOT=1, has_joined_once = 0 (test mode)");
  return 0;
#endif
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    LOG_WRN("Join state: EEPROM not available, has_joined_once = 0");
    ctx.has_joined_once = false;
    ctx.initialized = true;
    return 0;
  }

  struct join_state_record rec = {0};
  int ret = eeprom_read_join_state(&rec);
  if (ret != 0) {
    LOG_WRN("Join state read failed: %d, has_joined_once = 0", ret);
    ctx.has_joined_once = false;
    ctx.initialized = true;
    return 0;
  }

  if (rec.magic != JOIN_STATE_MAGIC) {
    ctx.has_joined_once = false;
    LOG_INF("Join state: first boot (no magic), has_joined_once = 0");
  } else {
    ctx.has_joined_once = (rec.has_joined_once != 0);
    LOG_INF("Join state: has_joined_once = %d", ctx.has_joined_once);
  }
  ctx.initialized = true;
  return 0;
}

int join_state_store_has_joined_once(bool *out) {
  if (out == NULL) {
    return -EINVAL;
  }
  if (!ctx.initialized) {
    (void)join_state_store_init();
  }
  *out = ctx.has_joined_once;
  return 0;
}

int join_state_store_set_has_joined_once(void) {
  if (!ctx.initialized) {
    (void)join_state_store_init();
  }
#if EEPROM_JOIN_STATE_CLEAR_ON_BOOT
  /* Test mode: update in-RAM only so next boot still acts as first boot */
  ctx.has_joined_once = true;
  LOG_INF("Join state: set has_joined_once = 1 (RAM only, CLEAR_ON_BOOT=1)");
  return 0;
#endif
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }

  struct join_state_record rec = {0};
  rec.magic = JOIN_STATE_MAGIC;
  rec.has_joined_once = 1;

  int ret = eeprom_write_join_state(&rec);
  if (ret != 0) {
    LOG_ERR("Join state write failed: %d", ret);
    return ret;
  }
  ctx.has_joined_once = true;
  LOG_INF("Join state: set has_joined_once = 1 (persisted)");
  return 0;
}
