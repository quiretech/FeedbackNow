#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

/*
 * FlexBox v1.2 — Single point for FRD-configurable parameters.
 * Sections: LoRa, Buttons/Input, Combo & mode timeouts, LED, EEPROM, RTC/Time
 * sync. Values marked * in FRD are tunable here.
 *
 * --- Production lock-down (ensure these before release) ---
 * LORA_JOIN_BACKOFF_HOURS     : 0 = test (1 min backoff), prod = 24 (or per
 * FRD). HEARTBEAT_USE_DEVEUI_JITTER : 1 = prod (daily + jitter), 0 = test
 * (every 120s). EEPROM_*_FACTORY_RESET_ON_BOOT : all 0 for prod (no wipe on
 * boot). EEPROM_JOIN_STATE_CLEAR_ON_BOOT : 0 for prod (persist join state).
 * SYS_CONFIG_EEPROM_PROBE_LOG : 0 for prod (no hex dump at boot).
 * RTC_FORCE_SET_TIME_ON_BOOT  : 0 for prod (don't overwrite RTC).
 * eui_keys.h                  : use production DevEUI/JoinEUI/AppKey; consider
 * excluding from VCS.
 */

/* =============================================================================
 * LoRa (FRD 4.5)
 * =============================================================================
 */
#define LORA_MAX_PAYLOAD_SIZE 11
#define LORA_MSGQ_SIZE 30
#define LORA_MESSAGE_ALIGNMENT 4
#define LORA_THREAD_STACK_SIZE 2048
#define LORA_THREAD_PRIORITY 7
#define LORA_JOIN_RETRY_DELAY_SECONDS 20
/** Number of join attempts in one "cycle" before assuming genuine failure (e.g.
 * no gateway). */
#define LORA_JOIN_ATTEMPTS_PER_CYCLE 20
/** After all attempts in a cycle fail, wait this many hours before next join
 * cycle (deployed device cannot be re-joined by human). Must be integer
 * (K_HOURS expects int). Use 0 for testing (1-minute backoff); production: 24.
 */
#define LORA_JOIN_BACKOFF_HOURS 6 // prod: changed to 6 after customer handoff
#define LORA_MAX_RETRIES 5
#define LORA_SEND_BUSY_RETRY_MS 3000
/** Minimum interval (ms) between uplink transmissions. Enforced by LoRa thread
 * after each send to stay within duty cycle / LoRa Alliance fair use. Set to 0
 * to disable. Typical: 2000–5000 ms (e.g. EU868 1% duty cycle). */
#define LORA_UPLINK_MIN_INTERVAL_MS 3000
/** After this many consecutive lorawan_send() failures, clear joined and
 * schedule join backoff (re-join after LORA_JOIN_BACKOFF_HOURS). Set to 0 to
 * disable. Typical: 3. */
#define LORA_SEND_FAILURES_BEFORE_BACKOFF 3
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
#define BUTTON_COOLDOWN_MS 7000

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
#define COMBO_REBOOT_HOLD_MS 8000      /* 0+1+2+3 in Staff: reboot */

/* =============================================================================
 * Mode timeouts (FRD 3.3, 3.4) — return to Normal when elapsed
 * =============================================================================
 */
/* Staff: 20s (longer than FRD 10s* to allow Reboot combo 0+1+2+3 hold 10s). */
#define STAFF_TIMEOUT_MS 20000
#define DEVICE_INFO_TIMEOUT_MS                                                 \
  15000 /* Increased for better UX (was 4s, display delay is 5s) */
#define REBOOT_LED_MS 3000

/* =============================================================================
 * LED (FRD 4.4) — on/off only; patterns are blink counts and timings
 * =============================================================================
 */
#define NUM_LEDS 1
/* Button press accepted: LED solid ON for 1s, then OFF */
#define LED_BUTTON_ACCEPTED_MS 1000
/* Join success: LED blinks 3 times (3 x 60ms ON, 60ms OFF; total ~360ms) */
#define LED_JOIN_BLINKS 3
#define LED_JOIN_ON_MS 60
#define LED_JOIN_OFF_MS 60
/* While joining (no EPD): LED ON for 2s, then OFF for 1s, repeats until done */
#define LED_JOINING_ON_MS 2000
#define LED_JOINING_OFF_MS 1000
/* NFC waiting: LED blinks at 1 Hz (500ms ON, 500ms OFF) */
#define LED_NFC_1HZ_ON_MS 500
#define LED_NFC_1HZ_OFF_MS 500
/* NFC read fail: LED blinks 2 times (2 x 400ms ON, 200ms OFF; total ~800ms) */
#define LED_NFC_FAIL_BLINKS 2
#define LED_NFC_FAIL_ON_MS 400
#define LED_NFC_FAIL_OFF_MS 200
/* Check-in/out/Registered vote: LED blinks 2 times (2 x 150ms ON, 100ms OFF;
 * total ~400ms) */
#define LED_CONFIRM_BLINKS 2
#define LED_CONFIRM_ON_MS 150
#define LED_CONFIRM_OFF_MS 100
/* Reboot indication: LED solid ON for 3s, then OFF (reboot handled elsewhere)
 */
#define LED_REBOOT_HOLD_MS 3000
/* Power on: LED blinks 2 times (2 x 100ms ON; total ~200ms) */
#define LED_POWER_ON_BLINKS 2
#define LED_POWER_ON_MS 100

/* =============================================================================
 * EEPROM / persistent storage
 * =============================================================================
 */
#define EEPROM_COUNTERS_FACTORY_RESET_ON_BOOT                                  \
  0 /* prod: 0; 1 = one-shot wipe on boot */
#define EEPROM_DEVNONCE_FACTORY_RESET_ON_BOOT                                  \
  0 /* prod: 0; 1 = one-shot reinit on boot */
#define EEPROM_JOIN_STATE_CLEAR_ON_BOOT                                        \
  0 /* prod: 0; 1 = test (no persist join state) */
/* If 1, log EEPROM probe hex dump at boot (diagnostic). prod: 0 */
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
#define RTC_FORCE_SET_TIME_ON_BOOT                                             \
  0 /* prod: 0 (do not overwrite RTC from build-time) */
#define RTC_SET_YEAR 2026
#define RTC_SET_MONTH 1
#define RTC_SET_DAY 1
#define RTC_SET_HOUR 00
#define RTC_SET_MINUTE 00
#define RTC_SET_SECOND 00
#define RTC_VALID_YEAR_MIN 2026
/** Retries for RTC read when rail may have just powered up (I2C -EIO). */
#define RTC_GET_EPOCH_RETRIES 3
/** Delay (ms) between RTC read retries. */
#define RTC_GET_EPOCH_RETRY_DELAY_MS 5
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
 * prod: 1 */
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
/** Cleaning screen auto-revert to last cleaned if no check-out (ms).
 * prod: 45; test: 1–3. */
#define EPD_CLEANING_REVERT_MINUTES 45U
#define EPD_CLEANING_AUTO_REVERT_MS (EPD_CLEANING_REVERT_MINUTES * 60U * 1000U)
/**
 * Delay (ms) before running display work. EPD and LoRa share SPI; holding the
 * bus for EPD refresh blocks the radio during RX windows and drops downlinks.
 * Delay allows RX1/RX2 (~1–2 s after uplink) to complete before we use SPI.
 * Set to 0 to disable delay (e.g. if SPI is not shared).
 */
#define DISPLAY_WORK_DELAY_MS 5000

/* =============================================================================
 * Thread and message queue sizing (single source of truth for prod tuning)
 * =============================================================================
 * Tune these with stack usage reports (-fstack-usage / CONFIG_STACK_SENTINEL).
 * Msgq sizes should cover burst traffic; too small => -ENOMEM, too large =>
 * RAM.
 */
#define SMF_MSGQ_SIZE 16
#define SMF_MSGQ_ALIGN 4
#define SMF_THREAD_STACK_SIZE 1536
#define DISPLAY_JOB_QUEUE_SIZE 8
#define DISPLAY_JOB_ALIGN 4
#define LED_UI_MSGQ_LEN 8
#define LED_UI_MSGQ_ALIGN 4
#define LED_UI_THREAD_STACK 1024
#define HOUSEKEEPING_STACK_SIZE 1024
#define NFC_WORKER_STACK_SIZE 1024

/* =============================================================================
 * Firmware Version
 * =============================================================================
 */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 2
#define FW_VERSION_PATCH 0
#define FW_VERSION_STRING "1.2.0"

#endif /* SYS_CONFIG_H */
