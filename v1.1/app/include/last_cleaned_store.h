/**
 * Last cleaned display store (EEPROM).
 * Holds the epoch timestamp shown on the EPD "last cleaned" screen.
 * Written by: NFC check-out (RTC value), downlink 0x01 when applied.
 * RTC remains the gold standard for uplinks; this store is display-only.
 */
#ifndef LAST_CLEANED_STORE_H
#define LAST_CLEANED_STORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Call once after EEPROM is available (e.g. after button_counter_store_init). */
int last_cleaned_store_init(void);

/**
 * Read the stored epoch. If never written, returns false and *epoch_out unchanged.
 * @return true if store has a valid value, false if empty/unreadable
 */
bool last_cleaned_store_get(uint32_t *epoch_out);

/**
 * Write epoch to store (e.g. from RTC on check-out, or from downlink 0x01 payload).
 */
int last_cleaned_store_set(uint32_t epoch);

/**
 * If store is empty (never written), write current_epoch and return 0.
 * Used at boot for "visual symmetry" so first screen shows a valid time.
 */
int last_cleaned_store_ensure_non_empty(uint32_t current_epoch);

#ifdef __cplusplus
}
#endif

#endif /* LAST_CLEANED_STORE_H */
