/**
 * @file config_store.c
 * @brief Dynamic configuration store implementation backed by external AT24 EEPROM.
 */

#include "config_store.h"
#include "eeprom_layout.h"
#include "eui_keys.h"
#include "onboarding_config.h"
#include "rail_manager.h"
#include "rtc.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(config_store, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
#define EEPROM_NODE DT_NODELABEL(eeprom0)
static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_NODE);
#else
static const struct device *eeprom_dev = NULL;
#endif

static struct {
  struct k_mutex lock;
  bool initialized;
  bool is_provisioned;
  struct fbn_config_record active_cfg;
} store_ctx;

/* Compile-time fallback values */
static const uint8_t default_dev_eui[8]   = LORAWAN_DEV_EUI;
static const uint8_t default_join_eui[8]  = LORAWAN_JOIN_EUI;
static const uint8_t default_app_key[16]  = LORAWAN_APP_KEY;

#if defined(CONFIG_LORAMAC_REGION_US915)
static const uint8_t default_region = LORA_REGION_US915;
#elif defined(CONFIG_LORAMAC_REGION_EU868)
static const uint8_t default_region = LORA_REGION_EU868;
#else
static const uint8_t default_region = LORA_REGION_UNSPECIFIED;
#endif

static uint32_t calc_record_crc(const struct fbn_config_record *rec) {
  size_t data_len = offsetof(struct fbn_config_record, crc32);
  return crc32_ieee((const uint8_t *)rec, data_len);
}

static void load_compile_time_defaults(struct fbn_config_record *rec) {
  memset(rec, 0, sizeof(*rec));
  rec->magic = CONFIG_STORE_MAGIC;
  rec->version = CONFIG_STORE_VERSION;
  rec->lora_region = default_region;
  memcpy(rec->dev_eui, default_dev_eui, sizeof(rec->dev_eui));
  memcpy(rec->join_eui, default_join_eui, sizeof(rec->join_eui));
  memcpy(rec->app_key, default_app_key, sizeof(rec->app_key));
  strncpy(rec->unit_id, DEVICE_UNIT_ID_STRING, sizeof(rec->unit_id) - 1);
  strncpy(rec->client_name, DEVICE_REGISTRY_CLIENT_NAME, sizeof(rec->client_name) - 1);
  rec->provision_epoch = 0;
  rec->crc32 = calc_record_crc(rec);
}

int config_store_init(void) {
  k_mutex_init(&store_ctx.lock);
  k_mutex_lock(&store_ctx.lock, K_FOREVER);

  if (store_ctx.initialized) {
    k_mutex_unlock(&store_ctx.lock);
    return 0;
  }

  load_compile_time_defaults(&store_ctx.active_cfg);
  store_ctx.is_provisioned = false;

  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    LOG_WRN("EEPROM device not ready, using compile-time config");
    store_ctx.initialized = true;
    k_mutex_unlock(&store_ctx.lock);
    return 0;
  }

  struct fbn_config_record eeprom_rec;
  rail_manager_request_3v3a();
  int ret = eeprom_read(eeprom_dev, (off_t)EEPROM_CONFIG_STORE_OFF, &eeprom_rec,
                        sizeof(eeprom_rec));
  rail_manager_release_3v3a();

  if (ret != 0) {
    LOG_ERR("Failed to read config from EEPROM: %d", ret);
  } else {
    uint32_t expected_crc = calc_record_crc(&eeprom_rec);
    if (eeprom_rec.magic == CONFIG_STORE_MAGIC &&
        eeprom_rec.version == CONFIG_STORE_VERSION &&
        eeprom_rec.crc32 == expected_crc) {
      LOG_INF("Valid provisioned config loaded from EEPROM for Unit ID: %s",
              eeprom_rec.unit_id);
      store_ctx.active_cfg = eeprom_rec;
      store_ctx.is_provisioned = true;
    } else {
      LOG_INF("No provisioned config in EEPROM (magic=0x%08X), using compile-time defaults",
              (unsigned int)eeprom_rec.magic);
    }
  }

  store_ctx.initialized = true;
  k_mutex_unlock(&store_ctx.lock);
  return 0;
}

bool config_store_is_provisioned(void) {
  return store_ctx.is_provisioned;
}

void config_store_get_dev_eui(uint8_t out[8]) {
  k_mutex_lock(&store_ctx.lock, K_FOREVER);
  memcpy(out, store_ctx.active_cfg.dev_eui, 8);
  k_mutex_unlock(&store_ctx.lock);
}

void config_store_get_join_eui(uint8_t out[8]) {
  k_mutex_lock(&store_ctx.lock, K_FOREVER);
  memcpy(out, store_ctx.active_cfg.join_eui, 8);
  k_mutex_unlock(&store_ctx.lock);
}

void config_store_get_app_key(uint8_t out[16]) {
  k_mutex_lock(&store_ctx.lock, K_FOREVER);
  memcpy(out, store_ctx.active_cfg.app_key, 16);
  k_mutex_unlock(&store_ctx.lock);
}

uint8_t config_store_get_region(void) {
  k_mutex_lock(&store_ctx.lock, K_FOREVER);
  uint8_t reg = store_ctx.active_cfg.lora_region;
  k_mutex_unlock(&store_ctx.lock);
  return reg;
}

const char *config_store_get_unit_id(void) {
  return store_ctx.active_cfg.unit_id;
}

const char *config_store_get_client_name(void) {
  return store_ctx.active_cfg.client_name;
}

uint32_t config_store_get_provision_epoch(void) {
  return store_ctx.active_cfg.provision_epoch;
}

static int commit_record_to_eeprom(const struct fbn_config_record *rec) {
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }
  rail_manager_request_3v3a();
  int ret = eeprom_write(eeprom_dev, (off_t)EEPROM_CONFIG_STORE_OFF, rec,
                         sizeof(*rec));
  rail_manager_release_3v3a();
  if (ret != 0) {
    LOG_ERR("EEPROM config write failed: %d", ret);
    return ret;
  }
  return 0;
}

int config_store_set_lora_keys(const uint8_t dev_eui[8], const uint8_t join_eui[8],
                               const uint8_t app_key[16], uint8_t region) {
  k_mutex_lock(&store_ctx.lock, K_FOREVER);

  memcpy(store_ctx.active_cfg.dev_eui, dev_eui, 8);
  memcpy(store_ctx.active_cfg.join_eui, join_eui, 8);
  memcpy(store_ctx.active_cfg.app_key, app_key, 16);
  if (region != LORA_REGION_UNSPECIFIED) {
    store_ctx.active_cfg.lora_region = region;
  }

  uint32_t now = 0;
  (void)rtc_get_epoch_seconds(&now);
  store_ctx.active_cfg.provision_epoch = now;

  store_ctx.active_cfg.magic = CONFIG_STORE_MAGIC;
  store_ctx.active_cfg.version = CONFIG_STORE_VERSION;
  store_ctx.active_cfg.crc32 = calc_record_crc(&store_ctx.active_cfg);

  int ret = commit_record_to_eeprom(&store_ctx.active_cfg);
  if (ret == 0) {
    store_ctx.is_provisioned = true;
    LOG_INF("LoRa keys updated in EEPROM");
  }

  k_mutex_unlock(&store_ctx.lock);
  return ret;
}

int config_store_set_identity(const char *unit_id, const char *client_name) {
  k_mutex_lock(&store_ctx.lock, K_FOREVER);

  if (unit_id != NULL && strlen(unit_id) > 0) {
    memset(store_ctx.active_cfg.unit_id, 0, sizeof(store_ctx.active_cfg.unit_id));
    strncpy(store_ctx.active_cfg.unit_id, unit_id, sizeof(store_ctx.active_cfg.unit_id) - 1);
  }
  if (client_name != NULL) {
    memset(store_ctx.active_cfg.client_name, 0, sizeof(store_ctx.active_cfg.client_name));
    strncpy(store_ctx.active_cfg.client_name, client_name, sizeof(store_ctx.active_cfg.client_name) - 1);
  }

  store_ctx.active_cfg.magic = CONFIG_STORE_MAGIC;
  store_ctx.active_cfg.version = CONFIG_STORE_VERSION;
  store_ctx.active_cfg.crc32 = calc_record_crc(&store_ctx.active_cfg);

  int ret = commit_record_to_eeprom(&store_ctx.active_cfg);
  if (ret == 0) {
    store_ctx.is_provisioned = true;
    LOG_INF("Unit identity updated in EEPROM: %s", store_ctx.active_cfg.unit_id);
  }

  k_mutex_unlock(&store_ctx.lock);
  return ret;
}

int config_store_factory_reset(void) {
  k_mutex_lock(&store_ctx.lock, K_FOREVER);

  uint8_t zero_buf[sizeof(struct fbn_config_record)] = {0};
  int ret = 0;
  if (eeprom_dev != NULL && device_is_ready(eeprom_dev)) {
    rail_manager_request_3v3a();
    ret = eeprom_write(eeprom_dev, (off_t)EEPROM_CONFIG_STORE_OFF, zero_buf,
                       sizeof(zero_buf));
    rail_manager_release_3v3a();
  }

  load_compile_time_defaults(&store_ctx.active_cfg);
  store_ctx.is_provisioned = false;
  LOG_INF("Config store reset to compile-time defaults");

  k_mutex_unlock(&store_ctx.lock);
  return ret;
}
