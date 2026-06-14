/**
 * Timezone offset store — EEPROM-backed minutes-from-UTC for EPD display.
 * Layout: magic 2B (0x5A54) + int16_t offset 2B (native byte order).
 */
#include "tz_offset_store.h"
#include "sys_config.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tz_offset_store, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
#define EEPROM_NODE DT_NODELABEL(eeprom0)
static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_NODE);
#else
static const struct device *eeprom_dev = NULL;
#endif

#define TZ_MAGIC 0x5A54U

static struct {
  bool initialized;
  int16_t cached; /* valid only after successful init + read */
  bool cached_valid;
} ctx;

static int clamp_offset(int16_t minutes) {
  if (minutes < TZ_OFFSET_MIN_MINUTES) {
    return TZ_OFFSET_MIN_MINUTES;
  }
  if (minutes > TZ_OFFSET_MAX_MINUTES) {
    return TZ_OFFSET_MAX_MINUTES;
  }
  return minutes;
}

int tz_offset_store_init(void) {
  if (ctx.initialized) {
    return 0;
  }
  ctx.initialized = true;
  ctx.cached_valid = false;
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    LOG_WRN("TZ offset store: EEPROM not available");
    return -ENODEV;
  }

  uint8_t init_buf[EEPROM_TZ_OFFSET_SIZE];
  int ret = eeprom_read(eeprom_dev, (off_t)EEPROM_TZ_OFFSET_OFF, init_buf,
                       EEPROM_TZ_OFFSET_SIZE);
  uint16_t magic = 0xFFFFU;
  int16_t val = 0;
  if (ret != 0) {
    LOG_WRN("TZ offset store read failed: %d", ret);
    magic = 0;
    val = (int16_t)DEFAULT_TIMEZONE_OFFSET_MINUTES;
    val = (int16_t)clamp_offset(val);
  } else {
    magic = (uint16_t)init_buf[0] << 8 | (uint16_t)init_buf[1];
    if (magic == TZ_MAGIC) {
      val = (int16_t)((uint16_t)init_buf[2] << 8 | (uint16_t)init_buf[3]);
    } else {
      val = (int16_t)DEFAULT_TIMEZONE_OFFSET_MINUTES;
      val = (int16_t)clamp_offset(val);
    }
  }
  if (magic != TZ_MAGIC) {
    /* Uninitialized: write default */
    val = (int16_t)DEFAULT_TIMEZONE_OFFSET_MINUTES;
    val = (int16_t)clamp_offset(val);
    uint8_t buf[EEPROM_TZ_OFFSET_SIZE];
    buf[0] = (uint8_t)(TZ_MAGIC >> 8);
    buf[1] = (uint8_t)(TZ_MAGIC & 0xFF);
    buf[2] = (uint8_t)((uint16_t)val >> 8);
    buf[3] = (uint8_t)((uint16_t)val & 0xFF);
    ret = eeprom_write(eeprom_dev, (off_t)EEPROM_TZ_OFFSET_OFF, buf,
                       EEPROM_TZ_OFFSET_SIZE);
    if (ret != 0) {
      LOG_ERR("TZ offset store write default failed: %d", ret);
      return ret;
    }
    LOG_DBG("TZ offset store: initialized with default %d min (UTC%+d)",
            (int)val, (int)val / 60);
  }
  /* val already set from init_buf when magic was TZ_MAGIC, or from default */
  ctx.cached = val;
  ctx.cached_valid = true;
  LOG_DBG("TZ offset store init OK, offset=%d min", (int)ctx.cached);
  return 0;
}

int tz_offset_store_get(int16_t *out_minutes) {
  if (out_minutes == NULL) {
    return -EINVAL;
  }
  if (!ctx.initialized) {
    (void)tz_offset_store_init();
  }
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    *out_minutes = (int16_t)DEFAULT_TIMEZONE_OFFSET_MINUTES;
    return -ENODEV;
  }
  if (ctx.cached_valid) {
    *out_minutes = ctx.cached;
    return 0;
  }
  uint8_t buf[EEPROM_TZ_OFFSET_SIZE];
  int ret = eeprom_read(eeprom_dev, (off_t)EEPROM_TZ_OFFSET_OFF, buf,
                        EEPROM_TZ_OFFSET_SIZE);
  if (ret != 0) {
    *out_minutes = (int16_t)DEFAULT_TIMEZONE_OFFSET_MINUTES;
    return ret;
  }
  uint16_t magic = (uint16_t)buf[0] << 8 | (uint16_t)buf[1];
  if (magic != TZ_MAGIC) {
    *out_minutes = (int16_t)DEFAULT_TIMEZONE_OFFSET_MINUTES;
    return -ENODATA;
  }
  int16_t val = (int16_t)((uint16_t)buf[2] << 8 | (uint16_t)buf[3]);
  ctx.cached = val;
  ctx.cached_valid = true;
  *out_minutes = val;
  return 0;
}

int tz_offset_store_set(int16_t minutes) {
  if (!ctx.initialized) {
    (void)tz_offset_store_init();
  }
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }
  minutes = (int16_t)clamp_offset(minutes);
  uint8_t buf[EEPROM_TZ_OFFSET_SIZE];
  buf[0] = (uint8_t)(TZ_MAGIC >> 8);
  buf[1] = (uint8_t)(TZ_MAGIC & 0xFF);
  buf[2] = (uint8_t)((uint16_t)minutes >> 8);
  buf[3] = (uint8_t)((uint16_t)minutes & 0xFF);
  int ret = eeprom_write(eeprom_dev, (off_t)EEPROM_TZ_OFFSET_OFF, buf,
                         EEPROM_TZ_OFFSET_SIZE);
  if (ret != 0) {
    LOG_ERR("TZ offset store write failed: %d", ret);
    return ret;
  }
  ctx.cached = minutes;
  ctx.cached_valid = true;
  LOG_DBG("TZ offset store set %d min (UTC%+d)", (int)minutes, (int)minutes / 60);
  return 0;
}
