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
 * @brief Read battery voltage in millivolts.
 *
 * Synchronously reads one ADC sample and converts to VBAT millivolts using
 * the configured resistor divider. Caller should ensure any required power
 * rails (e.g. 3.3A) are enabled.
 *
 * @param battery_mv Output millivolts.
 * @return 0 on success, negative errno on failure.
 */
int battery_adc_read_mv(int32_t *battery_mv);

#endif /* BATTERY_ADC_H */

