#ifndef DEVNONCE_STORE_H
#define DEVNONCE_STORE_H

#include <stdint.h>

/**
 * @brief Initialize persistent DevNonce storage.
 *
 * Uses external EEPROM (eeprom0). On failure returns negative errno.
 */
int devnonce_store_init(void);

/**
 * @brief Allocate the next DevNonce value (monotonic) and persist it.
 *
 * This function reserves the value in EEPROM before returning it, so that
 * even if the device resets mid-join, the same DevNonce will not be reused.
 *
 * @param out_nonce Output DevNonce (0..65535)
 * @return 0 on success, negative errno on failure.
 */
int devnonce_store_next(uint16_t *out_nonce);

/**
 * @brief Factory reset: erase DevNonce store and reinitialize so next join
 *        uses devnonce 0.
 *
 * Erases both EEPROM slots and reinitializes; next devnonce_store_next() will
 * return 0. Use via downlink 0x06 or EEPROM_DEVNONCE_FACTORY_RESET_ON_BOOT.
 *
 * @return 0 on success, negative errno on failure.
 */
int devnonce_store_factory_reset(void);

#endif /* DEVNONCE_STORE_H */
