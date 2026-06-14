#ifndef BATTERY_ADC_H
#define BATTERY_ADC_H

#include <stdint.h>

/**
 * @brief Initialize battery ADC channel (AIN3 on nRF52840DK).
 *
 * Call once at boot (after rails/I2C are ready). Returns 0 on success.
 */
int battery_adc_init(void);

/**
 * @brief Read battery voltage in millivolts and refresh the shared cache.
 *
 * Synchronously reads one ADC sample and converts to VBAT millivolts using
 * the configured resistor divider. On success, updates last_battery_mv and the
 * LoRaWAN DevStatus level (same cache used by device-info EPD). Caller should
 * ensure any required power rails (e.g. 3.3A) are enabled.
 *
 * Called at boot (main) and on each housekeeping run.
 *
 * @param battery_mv Output millivolts.
 * @return 0 on success, negative errno on failure.
 */
int battery_adc_read_mv(int32_t *battery_mv);

/**
 * Update shared battery cache from a known mV value (LoRaWAN level derived).
 * Prefer battery_adc_read_mv(); this remains for explicit cache writes.
 */
void battery_adc_lorawan_cache_set_from_mv(int32_t battery_mv);

/**
 * Last cached battery voltage in millivolts (boot sample or latest HK read).
 * Returns -ENODATA if unknown.
 */
int battery_adc_last_mv_get(int32_t *battery_mv);

/**
 * Format cached mV as "x.xxx v" for EPD (integer math; no float printf).
 * Returns 0 on success, -EINVAL on bad args.
 */
int battery_adc_format_mv_display(int32_t battery_mv, char *buf, size_t buf_len);

/**
 * LoRaWAN battery callback value: 1..254 from last cache update, 255 if
 * unknown (no successful read yet). Safe from any thread; O(1).
 */
uint8_t battery_adc_lorawan_level_get(void);

#endif /* BATTERY_ADC_H */

