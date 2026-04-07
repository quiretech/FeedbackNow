#ifndef EEPROM_PROBE_H
#define EEPROM_PROBE_H

/**
 * @brief Probe external EEPROM and log basic readiness/read results.
 *
 * Safe to call even if EEPROM is absent; it will log warnings and return.
 */
void eeprom_probe_log(void);

#endif /* EEPROM_PROBE_H */


