#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

/*
 * Demo mode switch
 * - 0: current pseudo simulation (periodic rotating payloads)
 * - 1: real button presses generate button uplinks
 */
#define DEMO_USE_REAL_BUTTON_UPLINK 1

/* LoRa Configuration */
#define LORA_MAX_PAYLOAD_SIZE 11
#define LORA_MSGQ_SIZE 10
#define LORA_MESSAGE_QUEUE_SIZE 10
#define LORA_MESSAGE_ALIGNMENT 4
#define LORA_THREAD_STACK_SIZE 2048
/* Step 9: Lower priority (higher number) allows idle thread to run more often.
 * Priority 7 = lower priority than before (5), reducing CPU usage when idle.
 * Still high enough to handle TX/RX operations promptly.
 */
#define LORA_THREAD_PRIORITY 7
#define LORA_JOIN_RETRY_DELAY_SECONDS 10
#define LORA_MAX_RETRIES 3
#define LORA_SEND_BUSY_RETRY_MS 1000
#define LORA_BUTTON_PORT 2
/* If join does not complete within the join wait timeout in main(), reboot. */
#define LORA_REBOOT_ON_JOIN_TIMEOUT 1

/* Send interval in seconds */
#define LORA_SEND_INTERVAL_SECONDS 900

/* Button configuration (gpio-keys aliases in DT overlay) */
#define NUM_BUTTONS 6
#define BUTTON_QUEUE_SIZE 16
#define BUTTON_QUEUE_ALIGNMENT 4
#define BUTTON_THREAD_STACK_SIZE 1536
/* Step 9: Lower priority (higher number) for button thread.
 * Priority 8 = lower priority than before (6), allowing more CPU idle time.
 * Button processing is not time-critical, so lower priority is acceptable.
 */
#define BUTTON_THREAD_PRIORITY 8
#define BUTTON_DEBOUNCE_MS 50
/* After any accepted press, ignore all further presses for this duration */
#define BUTTON_COOLDOWN_MS 5000

/* Combo hold durations (ms) — FRD 3.1; extend by adding entries in input layer
 */
#define COMBO_STAFF_HOLD_MS 2000       /* 0+1: enter Staff */
#define COMBO_DEVICE_INFO_HOLD_MS 3000 /* 0+1+5: Device Info */
#define COMBO_JOIN_HOLD_MS 3000        /* 0+1+2 in Staff: deliberate join */
#define COMBO_REBOOT_HOLD_MS 10000     /* 0+1+2+3 in Staff: reboot */

/* Mode timeouts (FRD 3.3, 3.4) — return to Normal when elapsed */
#define STAFF_TIMEOUT_MS 10000       /* Staff: 10s then Normal */
#define DEVICE_INFO_TIMEOUT_MS 30000 /* Device Info: 30s then Normal */
#define REBOOT_LED_MS 3000           /* Reboot: LED solid 3s then reboot */

/* LED configuration */
#define NUM_LEDS 1
#define LED_BLINK_DURATION_MS 1000

/* EEPROM / persistent counter maintenance */
#define EEPROM_COUNTERS_FACTORY_RESET_ON_BOOT 0
/* If 1, erase and reinitialize DevNonce store with new random value on boot */
#define EEPROM_DEVNONCE_FACTORY_RESET_ON_BOOT 0

/* EEPROM layout (external AT24 @ eeprom0). Keep regions non-overlapping.
 * - 0x0000..0x01FF : button_counter_store (2x256B slots)
 * - 0x0200..0x023F : devnonce_store (2x32B slots)
 */
#define EEPROM_COUNTER_STORE_BYTES 512U
#define EEPROM_DEVNONCE_SLOT_SIZE 32U
#define EEPROM_DEVNONCE_SLOT0_OFF ((uint32_t)EEPROM_COUNTER_STORE_BYTES)
#define EEPROM_DEVNONCE_SLOT1_OFF                                              \
  ((uint32_t)EEPROM_DEVNONCE_SLOT0_OFF + (uint32_t)EEPROM_DEVNONCE_SLOT_SIZE)

/* RTC demo configuration (Option B: hardcode time at boot) */
#define RTC_SET_TIME_ON_BOOT 1
/* If 1, always overwrite RTC time at boot (use once, then set back to 0) */
#define RTC_FORCE_SET_TIME_ON_BOOT 0

/* Set this to the current UTC time you want programmed into the RTC.
 * Notes:
 * - Month is 1-12
 * - Day is 1-31
 * - Hour is 0-23 (24h)
 * - Minute/second are 0-59
 */
#define RTC_SET_YEAR 2025
#define RTC_SET_MONTH 12
#define RTC_SET_DAY 24
#define RTC_SET_HOUR 21
#define RTC_SET_MINUTE 49
#define RTC_SET_SECOND 00

/* Treat RTC as "already initialized" if year >= this value */
#define RTC_VALID_YEAR_MIN 2024

/* Time sync behavior
 * If enabled, after a successful join we *require* DeviceTimeAns and a
 * successful RTC set within the window. If disabled, time sync is best-effort
 * (requested, but not gatekeeping boot).
 */
#define RTC_REQUIRE_LNS_TIME_SYNC 0
/* Total wait window (seconds) for DeviceTimeAns + RTC set after requesting
 * time. Only used when RTC_REQUIRE_LNS_TIME_SYNC=1.
 */
#define RTC_TIME_SYNC_REQUIRED_TIMEOUT_SECONDS 40

/*
 * LoRaWAN DeviceTime is typically reported as GPS seconds. To convert to UTC
 * epoch seconds, we subtract the GPS-UTC offset (leap seconds).
 *
 * As of 2017-01-01 through 2025, GPS-UTC is 18 seconds.
 */
#define LORAWAN_GPS_UTC_LEAP_SECONDS 18

#endif /* SYS_CONFIG_H */
