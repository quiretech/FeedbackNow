#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

/*
 * FlexBox v1.2 — Firmware tunables (FRD-configurable parameters).
 *
 * Related headers:
 *   sys_config_profile.h  — PRODUCTION / DESK / LAB (change SYS_CONFIG_PROFILE)
 *   onboarding_config.h   — unit id, provision UTC, DEVICE_HW_VARIANT, registry strings
 *   eeprom_layout.h       — fixed EEPROM map (do not tune per deployment)
 *   mapek/mapek_config.h  — MAPE-K timeouts (derived from MAPEK_LAB_FAST_TEST)
 *   display_strings.h     — EPD UI copy (when EPD_ENABLED)
 *
 * Release checklist: SYS_CONFIG_PROFILE_PRODUCTION, eui_keys.h production keys.
 */

#include "sys_config_profile.h"
#include "onboarding_config.h"
#include "eeprom_layout.h"

/* =============================================================================
 * Product variant (from onboarding_config.h → EPD compile-time gate)
 * =============================================================================
 */
#if DEVICE_HW_VARIANT == FLEXBOX_PLUS
#define EPD_ENABLED 1
#define NFC_ENABLED 1
#elif DEVICE_HW_VARIANT == FLEXBOX
#define EPD_ENABLED 0
#define NFC_ENABLED 0
#else
#error "DEVICE_HW_VARIANT must be FLEXBOX (0) or FLEXBOX_PLUS (1)"
#endif

/* =============================================================================
 * LoRa (FRD 4.5)
 * =============================================================================
 */
#define LORA_MAX_PAYLOAD_SIZE 11
#ifndef LORA_MAX_DOWNLINK_FRMPAYLOAD_SIZE
#define LORA_MAX_DOWNLINK_FRMPAYLOAD_SIZE 51
#endif
#ifndef DISPLAY_DL_CUSTOM_TEXT_MAX
#define DISPLAY_DL_CUSTOM_TEXT_MAX 96
#endif
#ifndef DL_CUSTOM_TEXT_DEFAULT_MINUTES
#define DL_CUSTOM_TEXT_DEFAULT_MINUTES 5U
#endif
#define LORA_MSGQ_SIZE 30
#define LORA_MESSAGE_ALIGNMENT 4
#define LORA_THREAD_STACK_SIZE 2048
#define LORA_THREAD_PRIORITY 7
#define LORA_JOIN_RETRY_DELAY_SECONDS 60
#define LORA_JOIN_ATTEMPTS_PER_CYCLE 10
#if MAPEK_LAB_FAST_TEST
#define LORA_JOIN_BACKOFF_HOURS 0
#else
#define LORA_JOIN_BACKOFF_HOURS 1
#endif
#define LORA_SEND_BUSY_RETRY_MS 5000
#define LORA_UPLINK_MIN_INTERVAL_MS 3000
#define LORA_POST_JOIN_MAC_SETTLE_MS 0
#define LORA_POST_JOIN_MAC_PROBE_RETRIES 30
#define LORA_POST_JOIN_MAC_PROBE_RETRY_MS 500
#define LORA_POST_JOIN_ANS_SETTLE_MS 2500
#define LORA_INSTALL_EXTRA_LINK_SAMPLES 0
#define LORA_INSTALL_EXTRA_LINK_SPACING_MS 2000
/** MAPE-K monitor DL RSSI/SNR EWMA — see mapek/mapek_config.h for HB cadence. */
#ifndef MAPEK_LINK_EWMA_SHIFT
#define MAPEK_LINK_EWMA_SHIFT 3U
#endif
#ifndef MAPEK_RF_RSSI_GOOD_DB
#define MAPEK_RF_RSSI_GOOD_DB (-80)
#endif
#ifndef MAPEK_RF_RSSI_FAIR_DB
#define MAPEK_RF_RSSI_FAIR_DB (-95)
#endif
#ifndef MAPEK_RF_RSSI_POOR_DB
#define MAPEK_RF_RSSI_POOR_DB (-105)
#endif
#ifndef MAPEK_RF_SNR_GOOD_MIN
#define MAPEK_RF_SNR_GOOD_MIN 7
#endif
#ifndef MAPEK_RF_SNR_FAIR_MIN
#define MAPEK_RF_SNR_FAIR_MIN 4
#endif
#ifndef MAPEK_RF_SNR_POOR_MIN
#define MAPEK_RF_SNR_POOR_MIN 2
#endif
#define LORA_BUTTON_PORT 2
#define LORA_BUTTON_UPLINK_CONFIRMED 0
#define LORA_NFC_UPLINK_CONFIRMED 0
#define LORA_HEARTBEAT_UPLINK_CONFIRMED 0
#define LORA_COUNTER_SYNC_CONFIRMED 0
#define COUNTER_SYNC_DELAY_MS 100
#define COUNTER_SYNC_JITTER_MAX_MS 600

/* =============================================================================
 * Buttons / Input (FRD 4.1)
 * =============================================================================
 */
#define NUM_BUTTONS 6
#define LORA_BURST_JOIN_TAIL_UPLINKS ((uint32_t)NUM_BUTTONS + 1U)
#define LORA_BURST_HK_TAIL_UPLINKS ((uint32_t)NUM_BUTTONS + 2U)
#define LORA_BURST_TIME_SYNC_TAIL_FUDGE_MS 5000U
#define LORA_BURST_COUNTER_JITTER_BUDGET_MS                                          \
  ((uint32_t)NUM_BUTTONS * (uint32_t)COUNTER_SYNC_JITTER_MAX_MS)
#define LORA_POST_COUNTER_BURST_TIME_SYNC_DELAY_JOIN_MS                              \
  (((uint32_t)LORA_BURST_JOIN_TAIL_UPLINKS * (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS) + \
   (uint32_t)LORA_BURST_TIME_SYNC_TAIL_FUDGE_MS +                                    \
   (uint32_t)LORA_BURST_COUNTER_JITTER_BUDGET_MS)
#define LORA_POST_COUNTER_BURST_TIME_SYNC_DELAY_HK_MS                                \
  (((uint32_t)LORA_BURST_HK_TAIL_UPLINKS * (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS) +   \
   (uint32_t)LORA_BURST_TIME_SYNC_TAIL_FUDGE_MS +                                    \
   (uint32_t)LORA_BURST_COUNTER_JITTER_BUDGET_MS)
/** Gap between deferred LinkCheckReq and DeviceTimeReq (after burst uplinks). */
#define LORA_POST_BURST_LINK_TO_TIME_GAP_MS                                        \
  ((uint32_t)LORA_UPLINK_MIN_INTERVAL_MS + (uint32_t)LORA_POST_JOIN_ANS_SETTLE_MS)

#define BUTTON_QUEUE_SIZE 16
#define BUTTON_QUEUE_ALIGNMENT 4
#define BUTTON_THREAD_STACK_SIZE 1536
#define BUTTON_THREAD_PRIORITY 8
#define BUTTON_DEBOUNCE_MS 50
#if EPD_ENABLED
#define BUTTON_COOLDOWN_MS 15000
#else
#define BUTTON_COOLDOWN_MS 5000
#endif
#define INPUT_COMBO_SCAN_INTERVAL_MS 50
#define INPUT_SESSION_RECOVERY_MS 250
#define COMBO_STAFF_HOLD_MS 1000
#define COMBO_DEVICE_INFO_HOLD_MS 2000
#define COMBO_JOIN_HOLD_MS 2000
#define COMBO_REBOOT_HOLD_MS 5000
#define STAFF_TIMEOUT_MS 20000
#define DEVICE_INFO_TIMEOUT_MS 20000
/** Max wait for LinkCheckAns after user opens device info (joined). Covers
 * lora_pace_uplink_spacing (up to LORA_UPLINK_MIN_INTERVAL_MS) plus RX windows. */
#define DEVICE_INFO_LINK_PROBE_TIMEOUT_MS                                        \
  (LORA_UPLINK_MIN_INTERVAL_MS + LORA_POST_JOIN_ANS_SETTLE_MS + 500U)
#define REBOOT_LED_MS 3000

/* =============================================================================
 * LED (FRD 4.4)
 * =============================================================================
 */
#define NUM_LEDS 1
#define LED_BUTTON_ACCEPTED_MS 600
#define LED_JOINING_ON_MS 800
#define LED_JOINING_OFF_MS 800
#define LED_JOIN_BLINKS 4
#define LED_JOIN_ON_MS 180
#define LED_JOIN_OFF_MS 180
#define POST_JOIN_LED_BEFORE_EPD_MS 1700
#define LED_NFC_WAITING_ON_MS 600
#define LED_NFC_WAITING_OFF_MS 600
#define LED_NFC_FAIL_BLINKS 3
#define LED_NFC_FAIL_ON_MS 160
#define LED_NFC_FAIL_OFF_MS 240
#define LED_CONFIRM_BLINKS 3
#define LED_CONFIRM_ON_MS 140
#define LED_CONFIRM_OFF_MS 140
#define LED_POWER_ON_BLINKS 3
#define LED_POWER_ON_MS 120
#define LED_POWER_ON_OFF_MS 120
#define LED_REBOOT_HOLD_MS 3000
#define LED_CONFIRM_SMF_BLOCK_MS 1000

/* =============================================================================
 * Timezone (display only; internals stay UTC)
 * =============================================================================
 */
#define DEFAULT_TIMEZONE_OFFSET_MINUTES (60) // UTC+1h
#define TZ_OFFSET_MIN_MINUTES           (-1440)
#define TZ_OFFSET_MAX_MINUTES           (1440)

/* =============================================================================
 * RTC / time sync
 * =============================================================================
 */
#define RTC_SET_TIME_ON_BOOT 1
/* RTC_PRESERVE_EXISTING_ON_BOOT — from sys_config_profile.h */
#define RTC_SET_YEAR 2026
#define RTC_SET_MONTH 1
#define RTC_SET_DAY 1
#define RTC_SET_HOUR 0
#define RTC_SET_MINUTE 0
#define RTC_SET_SECOND 0
#define RTC_VALID_YEAR_MIN 2026
#define RTC_GET_EPOCH_RETRIES 5
#define RTC_GET_EPOCH_RETRY_DELAY_MS 10
#define RTC_3V3A_SETTLE_MS 120
#define RTC_INIT_RETRY_COUNT 5
#define RTC_INIT_RETRY_DELAY_MS 30
#define LORAWAN_GPS_UTC_LEAP_SECONDS 18
#define GPS_TO_UNIX_EPOCH_OFFSET 315964800U
#define TIME_SYNC_MAX_RETRIES 3
#define TIME_SYNC_ANS_TIMEOUT_MS 12000

/* =============================================================================
 * Power gating (rail manager)
 * =============================================================================
 */
#define RAIL_MANAGER_3V3A_KEEPALIVE_MS 60000
#define RAIL_MANAGER_3V6_KEEPALIVE_MS 15000

/* =============================================================================
 * Housekeeping / Heartbeat (FRD 4.9)
 * =============================================================================
 */
#if MAPEK_LAB_FAST_TEST
#define HEARTBEAT_USE_DEVEUI_JITTER 0
#define HOUSEKEEPING_INTERVAL_SECONDS 120
#else
#define HEARTBEAT_USE_DEVEUI_JITTER 1
#define HOUSEKEEPING_INTERVAL_SECONDS 86400
#endif
#define HOUSEKEEPING_RAIL_ADC_SETTLE_MS 100
#define SECONDS_PER_DAY 86400
#define MINUTES_PER_DAY (24 * 60)

/* =============================================================================
 * NFC (PN5180, ISO15693)
 * =============================================================================
 */
#define NFC_READ_BLOCK 5
#define NFC_SCAN_PHASE1_MS 6000
#define NFC_SCAN_TOTAL_MS 12000
#define NFC_POWER_SETTLE_MS 1000
#define NFC_COLD_BOOT_OFF_MS 50
#define NFC_POLL_INTERVAL_MS 100
#define NFC_INIT_MAX_ATTEMPTS 3
#define NFC_RECOVERY_OFF_MS 50
#define NFC_RECOVERY_SETTLE_MS 300
#define NFC_RECOVERY_OFF_MS_ESCALATED 200
#define NFC_RECOVERY_SETTLE_MS_ESCALATED 500

/* =============================================================================
 * EPD timings and locale (EPD_ENABLED set above from DEVICE_HW_VARIANT)
 * =============================================================================
 */
#define EPD_THANKS_DISPLAY_MS 1500
#define EPD_CLEANING_REVERT_MINUTES 45U
#define EPD_CLEANING_AUTO_REVERT_MS (EPD_CLEANING_REVERT_MINUTES * 60U * 1000U)

#if EPD_ENABLED
#ifndef EPD_LOCALE_FR_BITMAPS
#define EPD_LOCALE_FR_BITMAPS 0
#endif
#include "display_strings.h"
#endif

#ifndef EPD_DEVICE_STATUS_COMMISSION_BOOT
#define EPD_DEVICE_STATUS_COMMISSION_BOOT 0
#endif
#if EPD_DEVICE_STATUS_COMMISSION_BOOT
#define EPD_DEVICE_STATUS_COMMISSION_MS 15000
#endif

#define DISPLAY_WORK_DELAY_MS 5000

/* =============================================================================
 * Thread and message queue sizing
 * =============================================================================
 */
#define SMF_MSGQ_SIZE 24
#define SMF_MSGQ_ALIGN 4
#define SMF_POST_EVENT_CRITICAL_TIMEOUT_MS 100U
#define SMF_THREAD_STACK_SIZE 3072
#define DISPLAY_JOB_QUEUE_SIZE 8
#define DISPLAY_JOB_ALIGN 4
#define LED_UI_MSGQ_LEN 8
#define LED_UI_MSGQ_ALIGN 4
#define LED_UI_THREAD_STACK 1280
#define HOUSEKEEPING_STACK_SIZE 1536
#define NFC_WORKER_STACK_SIZE 1536

/* =============================================================================
 * Firmware / hardware version (gen_euis.py reads FW/HW for registry CSV)
 * =============================================================================
 */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 4
#define FW_VERSION_PATCH 0
#define FW_VERSION_STRING "1.4.0"
#define HW_VERSION_MAJOR 1
#define HW_VERSION_MINOR 4
#define HW_VERSION_PATCH 1
#define HW_VERSION_STRING "1.4.1"

#endif /* SYS_CONFIG_H */
