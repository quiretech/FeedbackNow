#ifndef BUTTON_COUNTER_STORE_H
#define BUTTON_COUNTER_STORE_H

#include <stdint.h>

/**
 * @brief Initialize persistent per-button counters.
 *
 * EEPROM is mandatory: on failure, returns a negative errno.
 */
int button_counter_store_init(void);

/**
 * @brief Increment and return the counter for a given button.
 *
 * @param button_id 0..NUM_BUTTONS-1
 * @param out_counter Optional; receives new counter value.
 * @return 0 on success, negative errno on failure.
 */
int button_counter_store_inc(uint8_t button_id, uint32_t *out_counter);

/**
 * @brief Get current counter value for a given button.
 *
 * @param button_id 0..NUM_BUTTONS-1
 * @param out_counter Output.
 * @return 0 on success, negative errno on failure.
 */
int button_counter_store_get(uint8_t button_id, uint32_t *out_counter);

/**
 * @brief Factory reset the persistent counter area in EEPROM.
 *
 * Erases the counter slots and writes an initial zeroed record.
 *
 * @return 0 on success, negative errno on failure.
 */
int button_counter_store_factory_reset(void);

#endif /* BUTTON_COUNTER_STORE_H */
