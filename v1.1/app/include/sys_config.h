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
#define LORA_JOIN_RETRY_DELAY_SECONDS 2
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
#define DEVICE_INFO_TIMEOUT_MS 10000
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
#define LED_JOIN_ON_MS 100
#define LED_JOIN_OFF_MS 100
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
/** Last cleaned display store (epoch for EPD "last cleaned" screen). */
#define EEPROM_LAST_CLEANED_OFF 0x0250U
#define EEPROM_LAST_CLEANED_SIZE 4U
/** Timezone offset store (minutes from UTC for EPD display). Layout: magic 2B +
 * int16_t 2B. */
#define EEPROM_TZ_OFFSET_OFF 0x0254U
#define EEPROM_TZ_OFFSET_SIZE 4U

/* =============================================================================
 * Timezone (display only; all internals stay UTC)
 * =============================================================================
 * Offset in minutes from UTC. Written to EEPROM via downlink 0x05; used only
 * when formatting timestamps for the EPD so users see local time.
 */
/** Build-time default (e.g. -480 for San Francisco PST). Used when EEPROM block
 * is uninitialized. */
#define DEFAULT_TIMEZONE_OFFSET_MINUTES (-480)
/** Clamp range: ±24 hours in minutes. */
#define TZ_OFFSET_MIN_MINUTES (-1440)
#define TZ_OFFSET_MAX_MINUTES 1440

/* =============================================================================
 * RTC / time sync
 * =============================================================================
 */
#define RTC_SET_TIME_ON_BOOT 1
#define RTC_FORCE_SET_TIME_ON_BOOT 0
#define RTC_SET_YEAR 2026
#define RTC_SET_MONTH 1
#define RTC_SET_DAY 1
#define RTC_SET_HOUR 00
#define RTC_SET_MINUTE 00
#define RTC_SET_SECOND 00
#define RTC_VALID_YEAR_MIN 2026
#define RTC_REQUIRE_LNS_TIME_SYNC 0
#define RTC_TIME_SYNC_REQUIRED_TIMEOUT_SECONDS 10
#define LORAWAN_GPS_UTC_LEAP_SECONDS 18

/* =============================================================================
 * Power gating (rail manager)
 * =============================================================================
 */
/** 3.3A keep-alive (ms) after last release so delayed EEPROM flush (5s) can
 * run. */
#define RAIL_MANAGER_3V3A_KEEPALIVE_MS 6000

/* =============================================================================
 * Housekeeping / Heartbeat (FRD 4.9)
 * =============================================================================
 * Periodic worker runs RTC sync (DeviceTimeReq), link check, battery sample +
 * heartbeat uplink. When HEARTBEAT_USE_DEVEUI_JITTER=1, run is daily at
 * 00:00 UTC + (DevEUI[7]*256+DevEUI[6]) % 1440 minutes (FRD 4.9).
 */
/** When 1, heartbeat runs once per day at 00:00 UTC + DevEUI-based offset
 * (minutes). When 0, runs every HOUSEKEEPING_INTERVAL_SECONDS (e.g. for test).
 */
#define HEARTBEAT_USE_DEVEUI_JITTER 1
/** Fallback interval (seconds) when jitter is off or RTC unavailable. */
#define HOUSEKEEPING_INTERVAL_SECONDS 120
/** Seconds per day (for daily schedule). */
#define SECONDS_PER_DAY 86400
/** Minutes per day (for offset modulo). */
#define MINUTES_PER_DAY (24 * 60)

/* =============================================================================
 * NFC (PN5180, ISO15693) — FRD 4.x Staff check-in/out/registered vote
 * =============================================================================
 */
/** Block number to read for 4-byte card data (e.g. user ID or custom data). */
#define NFC_READ_BLOCK 5
/** Max time to wait for card read before posting timeout (ms). */
#define NFC_SCAN_TIMEOUT_MS 5000

/* =============================================================================
 * EPD (Variant A vs B) — set to 0 for build without display
 * =============================================================================
 */
#define EPD_ENABLED 1
/** Thanks screen duration before returning to last cleaned (ms). */
#define EPD_THANKS_DISPLAY_MS 5000
/** Cleaning screen auto-revert to last cleaned if no check-out (ms). */
#define EPD_CLEANING_REVERT_MINUTES 45
#define EPD_CLEANING_AUTO_REVERT_MS (EPD_CLEANING_REVERT_MINUTES * 60 * 1000)
/**
 * Delay (ms) before running display work. EPD and LoRa share SPI; holding the
 * bus for EPD refresh blocks the radio during RX windows and drops downlinks.
 * Delay allows RX1/RX2 (~1–2 s after uplink) to complete before we use SPI.
 * Set to 0 to disable delay (e.g. if SPI is not shared).
 */
#define DISPLAY_WORK_DELAY_MS 3000

/* =============================================================================
 * Firmware Version
 * =============================================================================
 */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 2
#define FW_VERSION_PATCH 0
#define FW_VERSION_STRING "1.2.0"

#endif /* SYS_CONFIG_H */
