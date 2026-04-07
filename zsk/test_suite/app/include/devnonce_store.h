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
 * @brief Factory reset: erase DevNonce store and reinitialize with random value.
 *
 * This erases both EEPROM slots and reinitializes with a new random starting
 * DevNonce. Use this if you're getting OTAA errors and want to start fresh.
 *
 * @return 0 on success, negative errno on failure.
 */
int devnonce_store_factory_reset(void);

#endif /* DEVNONCE_STORE_H */
