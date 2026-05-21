#ifndef EEPROM_LAYOUT_H
#define EEPROM_LAYOUT_H

/*
 * External AT24 EEPROM map (@ eeprom0). Fixed layout — do not change offsets
 * without a migration plan (devices in the field retain prior data).
 */

#define EEPROM_COUNTER_STORE_BYTES 512U
#define EEPROM_DEVNONCE_SLOT_SIZE 32U
#define EEPROM_DEVNONCE_SLOT0_OFF ((uint32_t)EEPROM_COUNTER_STORE_BYTES)
#define EEPROM_DEVNONCE_SLOT1_OFF                                              \
  ((uint32_t)EEPROM_DEVNONCE_SLOT0_OFF + (uint32_t)EEPROM_DEVNONCE_SLOT_SIZE)
#define EEPROM_JOIN_STATE_OFF 0x0240U
#define EEPROM_JOIN_STATE_SIZE 8U
/** Last cleaned display store (epoch for EPD "last cleaned" screen). */
#define EEPROM_LAST_CLEANED_OFF 0x0250U
#define EEPROM_LAST_CLEANED_SIZE 4U
/** Timezone offset store (minutes from UTC for EPD display). Layout: magic 2B +
 * int16_t 2B. */
#define EEPROM_TZ_OFFSET_OFF 0x0254U
#define EEPROM_TZ_OFFSET_SIZE 4U

#endif /* EEPROM_LAYOUT_H */
