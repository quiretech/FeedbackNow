/**
 * Timezone offset store (EEPROM).
 * Persists offset in minutes from UTC; used only when formatting timestamps
 * for the EPD so users see local time. All internal ops remain UTC.
 */
#ifndef TZ_OFFSET_STORE_H
#define TZ_OFFSET_STORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Call once after EEPROM is available (e.g. after last_cleaned_store_init). */
int tz_offset_store_init(void);

/**
 * Read the stored offset (minutes from UTC).
 * @param out_minutes Output; unchanged on failure.
 * @return 0 on success, negative errno on failure (e.g. uninitialized store).
 */
int tz_offset_store_get(int16_t *out_minutes);

/**
 * Write offset to EEPROM. Value is clamped to TZ_OFFSET_MIN/MAX before storing.
 * @param minutes Offset in minutes from UTC (e.g. -480 for PST).
 * @return 0 on success, negative errno on failure.
 */
int tz_offset_store_set(int16_t minutes);

/**
 * Perform a magic reset, which resets the timezone offset to the default.
 * @return 0 on success, negative errno on failure.
 */
int magic_reset_perform(void);

#ifdef __cplusplus
}
#endif

#endif /* TZ_OFFSET_STORE_H */
