/**
 * @file config_store.h
 * @brief Dynamic configuration store backed by external EEPROM with fallback
 *        to compile-time defaults (eui_keys.h / onboarding_config.h).
 *
 * Supports runtime BLE onboarding / provisioning of:
 *  - LoRaWAN DevEUI (8 bytes)
 *  - LoRaWAN JoinEUI / AppEUI (8 bytes)
 *  - LoRaWAN AppKey (16 bytes)
 *  - LoRaWAN Regional Frequency (US915=1, EU868=2, AU915=3, AS923=4)
 *  - Device Unit ID string (up to 16 bytes)
 *  - Client Name string (up to 24 bytes)
 */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_STORE_MAGIC       0x46424E43U /* 'FBNC' (FeedBackNow Config) */
#define CONFIG_STORE_VERSION     1U

#define CONFIG_STORE_UNIT_ID_LEN     16U
#define CONFIG_STORE_CLIENT_NAME_LEN 24U

enum lora_region_id {
  LORA_REGION_UNSPECIFIED = 0,
  LORA_REGION_US915 = 1,
  LORA_REGION_EU868 = 2,
  LORA_REGION_AU915 = 3,
  LORA_REGION_AS923 = 4,
};

struct __packed fbn_config_record {
  uint32_t magic;
  uint8_t  version;
  uint8_t  lora_region;
  uint8_t  reserved[2];
  uint8_t  dev_eui[8];
  uint8_t  join_eui[8];
  uint8_t  app_key[16];
  char     unit_id[CONFIG_STORE_UNIT_ID_LEN];
  char     client_name[CONFIG_STORE_CLIENT_NAME_LEN];
  uint32_t provision_epoch;
  uint32_t crc32;
};

/**
 * @brief Initialize config store from EEPROM. If invalid or unwritten, loads defaults.
 * @return 0 on success, negative errno on EEPROM failure.
 */
int config_store_init(void);

/**
 * @brief Check if valid runtime provisioned configuration exists in EEPROM.
 */
bool config_store_is_provisioned(void);

/**
 * @brief Get active DevEUI (from EEPROM if provisioned, else build-time default).
 */
void config_store_get_dev_eui(uint8_t out[8]);

/**
 * @brief Get active JoinEUI (from EEPROM if provisioned, else build-time default).
 */
void config_store_get_join_eui(uint8_t out[8]);

/**
 * @brief Get active AppKey (from EEPROM if provisioned, else build-time default).
 */
void config_store_get_app_key(uint8_t out[16]);

/**
 * @brief Get active LoRa region identifier.
 */
uint8_t config_store_get_region(void);

/**
 * @brief Get active Unit ID string.
 */
const char *config_store_get_unit_id(void);

/**
 * @brief Get active Client Name string.
 */
const char *config_store_get_client_name(void);

/**
 * @brief Get active provision epoch (0 if default compile-time keys).
 */
uint32_t config_store_get_provision_epoch(void);

/**
 * @brief Save new LoRa keys to EEPROM.
 */
int config_store_set_lora_keys(const uint8_t dev_eui[8], const uint8_t join_eui[8],
                               const uint8_t app_key[16], uint8_t region);

/**
 * @brief Save new Unit ID and Client Name to EEPROM.
 */
int config_store_set_identity(const char *unit_id, const char *client_name);

/**
 * @brief Factory reset dynamic configuration area in EEPROM (reverts to compile-time keys).
 */
int config_store_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_STORE_H */
