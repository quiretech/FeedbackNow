#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

/*
 * FlexBox v1.2 — Single point for FRD-configurable parameters.
 * Sections: LoRa, Buttons/Input, Combo & mode timeouts, LED, EEPROM, RTC/Time
 * sync. Values marked * in FRD are tunable here.
 */

/* =============================================================================
 * LoRa (FRD 4.5)
 * =============================================================================
 */
#define LORA_MAX_PAYLOAD_SIZE 11
#define LORA_MSGQ_SIZE 10
#define LORA_MESSAGE_ALIGNMENT 4
#define LORA_THREAD_STACK_SIZE 2048
#define LORA_THREAD_PRIORITY 7
#define LORA_JOIN_RETRY_DELAY_SECONDS 10
#define LORA_MAX_RETRIES 3
#define LORA_SEND_BUSY_RETRY_MS 1000
#define LORA_BUTTON_PORT 2

/* =============================================================================
 * Buttons / Input (FRD 4.1; gpio-keys aliases in DT overlay)
 * =============================================================================
 */
#define NUM_BUTTONS 6
#define BUTTON_QUEUE_SIZE 16
#define BUTTON_QUEUE_ALIGNMENT 4
#define BUTTON_THREAD_STACK_SIZE 1536
#define BUTTON_THREAD_PRIORITY 8
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_COOLDOWN_MS 5000

/* Input layer: combo scan period and session recovery (held→0 for this long =
 * reset). */
#define INPUT_COMBO_SCAN_INTERVAL_MS 50
#define INPUT_SESSION_RECOVERY_MS 300

/* =============================================================================
 * Combo hold durations (FRD 3.1) — extend by adding entries in input layer
 * =============================================================================
 */
#define COMBO_STAFF_HOLD_MS 2000       /* 0+1: enter Staff */
#define COMBO_DEVICE_INFO_HOLD_MS 3000 /* 0+1+5: Device Info */
#define COMBO_JOIN_HOLD_MS 3000        /* 0+1+2 in Staff: deliberate join */
#define COMBO_REBOOT_HOLD_MS 10000     /* 0+1+2+3 in Staff: reboot */

/* =============================================================================
 * Mode timeouts (FRD 3.3, 3.4) — return to Normal when elapsed
 * =============================================================================
 */
/* Staff: 20s (longer than FRD 10s* to allow Reboot combo 0+1+2+3 hold 10s). */
#define STAFF_TIMEOUT_MS 20000
#define DEVICE_INFO_TIMEOUT_MS 30000
#define REBOOT_LED_MS 3000

/* =============================================================================
 * LED (FRD 4.4) — on/off only; patterns are blink counts and timings
 * =============================================================================
 */
#define NUM_LEDS 1
/* Button accepted: solid on 1s then off */
#define LED_BUTTON_ACCEPTED_MS 1000
/* Join success: 3 quick flashes ~300ms total */
#define LED_JOIN_BLINKS 3
#define LED_JOIN_ON_MS 50
#define LED_JOIN_OFF_MS 50
/* NFC waiting: 1 Hz until next command */
#define LED_NFC_1HZ_ON_MS 500
#define LED_NFC_1HZ_OFF_MS 500
/* NFC read fail: 3 fast blinks ~600ms */
#define LED_NFC_FAIL_BLINKS 3
#define LED_NFC_FAIL_ON_MS 100
#define LED_NFC_FAIL_OFF_MS 100
/* Check-in/out/Registered vote: 3 blinks ~2s */
#define LED_CONFIRM_BLINKS 3
#define LED_CONFIRM_ON_MS 333
#define LED_CONFIRM_OFF_MS 333
/* Reboot: solid for this long then off (reboot handled elsewhere) */
#define LED_REBOOT_HOLD_MS 3000
/* Power on: 2 quick flashes ~200ms */
#define LED_POWER_ON_BLINKS 2
#define LED_POWER_ON_MS 100

/* =============================================================================
 * EEPROM / persistent storage
 * =============================================================================
 */
#define EEPROM_COUNTERS_FACTORY_RESET_ON_BOOT 0
#define EEPROM_DEVNONCE_FACTORY_RESET_ON_BOOT 0
#define EEPROM_JOIN_STATE_CLEAR_ON_BOOT 0
/* If 1, log EEPROM probe hex dump at boot (diagnostic). */
#define SYS_CONFIG_EEPROM_PROBE_LOG 0

/* EEPROM layout (external AT24 @ eeprom0). Keep regions non-overlapping. */
#define EEPROM_COUNTER_STORE_BYTES 512U
#define EEPROM_DEVNONCE_SLOT_SIZE 32U
#define EEPROM_DEVNONCE_SLOT0_OFF ((uint32_t)EEPROM_COUNTER_STORE_BYTES)
#define EEPROM_DEVNONCE_SLOT1_OFF                                              \
  ((uint32_t)EEPROM_DEVNONCE_SLOT0_OFF + (uint32_t)EEPROM_DEVNONCE_SLOT_SIZE)
#define EEPROM_JOIN_STATE_OFF 0x0240U
#define EEPROM_JOIN_STATE_SIZE 8U

/* =============================================================================
 * RTC / time sync
 * =============================================================================
 */
#define RTC_SET_TIME_ON_BOOT 1
#define RTC_FORCE_SET_TIME_ON_BOOT 0
#define RTC_SET_YEAR 2025
#define RTC_SET_MONTH 12
#define RTC_SET_DAY 24
#define RTC_SET_HOUR 21
#define RTC_SET_MINUTE 49
#define RTC_SET_SECOND 00
#define RTC_VALID_YEAR_MIN 2024
#define RTC_REQUIRE_LNS_TIME_SYNC 0
#define RTC_TIME_SYNC_REQUIRED_TIMEOUT_SECONDS 40
#define LORAWAN_GPS_UTC_LEAP_SECONDS 18

/* =============================================================================
 * Power gating (rail manager)
 * =============================================================================
 */
/** 3.3A keep-alive (ms) after last release so delayed EEPROM flush (5s) can run. */
#define RAIL_MANAGER_3V3A_KEEPALIVE_MS 6000

/* =============================================================================
 * Housekeeping / Heartbeat (FRD 4.9)
 * =============================================================================
 * Periodic worker runs RTC sync (DeviceTimeReq), and later: link check, battery
 * sample + heartbeat uplink. Interval is configurable for test (short) or
 * production (e.g. daily with jitter).
 */
/** Housekeeping run interval in seconds. Short for testing time sync; use
 * 86400 for daily heartbeat (FRD); add DevEUI jitter in future. */
#define HOUSEKEEPING_INTERVAL_SECONDS 60

#endif /* SYS_CONFIG_H */
