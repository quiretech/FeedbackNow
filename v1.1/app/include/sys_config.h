#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

/*
 * FlexBox v1.2 — Single point for FRD-configurable parameters.
 * Sections: LoRa, Buttons/Input, Combo & mode timeouts, LED, EEPROM, RTC/Time
 * sync. Values marked * in FRD are tunable here.
 *
 * --- Production lock-down (ensure these before release) ---
 * LORA_JOIN_BACKOFF_HOURS     : 0 = test (1 min), prod = 6–24 (per FRD).
 * HEARTBEAT_USE_DEVEUI_JITTER : 1 = prod (daily + jitter), 0 = test
 * (every 120s). EEPROM_*_FACTORY_RESET_ON_BOOT : all 0 for prod (no wipe on
 * boot). EEPROM_JOIN_STATE_CLEAR_ON_BOOT : 0 for prod (persist join state).
 * SYS_CONFIG_EEPROM_PROBE_LOG : 0 for prod (no hex dump at boot).
 * RTC_FORCE_SET_TIME_ON_BOOT  : 0 for prod (don't overwrite RTC).
 * eui_keys.h                  : use production DevEUI/JoinEUI/AppKey; consider
 * excluding from VCS.
 */

/* =============================================================================
 * Unit identity (written by onboarding/gen_euis.py — matches eui_registry)
 * =============================================================================
 */
/* BEGIN UNIT_ID (gen_euis.py) — do not edit by hand */
#define DEVICE_UNIT_ID_STRING "ZZ-UNIT-HM-TEST"
/* END UNIT_ID (gen_euis.py) */

/* =============================================================================
 * LoRa (FRD 4.5)
 * =============================================================================
 */
#define LORA_MAX_PAYLOAD_SIZE 11
#define LORA_MSGQ_SIZE 30
#define LORA_MESSAGE_ALIGNMENT 4
#define LORA_THREAD_STACK_SIZE 2048
#define LORA_THREAD_PRIORITY 7
/** Field-stable: 20s between join attempts (was 15). Reduces join-cycle load.
 */
#define LORA_JOIN_RETRY_DELAY_SECONDS 30
/** Number of join attempts in one "cycle" before assuming genuine failure (e.g.
 * no gateway). */
#define LORA_JOIN_ATTEMPTS_PER_CYCLE 20
/** After all attempts in a cycle fail, wait this many hours before next join
 * cycle (deployed device cannot be re-joined by human). Must be integer
 * (K_HOURS expects int). Use 0 for testing (1-min backoff); prod: 6–24 (per
 * FRD).
 *
 * BACKOFF TEST: set to 0 (1-min backoff), HEARTBEAT_USE_DEVEUI_JITTER=0,
 * LORA_SEND_FAILURES_BEFORE_BACKOFF=2, LORA_HEARTBEAT_UPLINK_CONFIRMED=1
 * to verify rejoin logic in ~5 min.
 */
#define LORA_JOIN_BACKOFF_HOURS 6 // prod: 6; test: 0
#define LORA_MAX_RETRIES 5
#define LORA_SEND_BUSY_RETRY_MS 5000
/** Minimum interval (ms) between uplink transmissions. Enforced by LoRa thread
 * after each send to stay within duty cycle / LoRa Alliance fair use. Set to 0
 * to disable. Typical: 2000–5000 ms (e.g. EU868 1% duty cycle). */
#define LORA_UPLINK_MIN_INTERVAL_MS 3000
/** After lorawan_join() succeeds, optional extra delay (ms) before posting
 * SMF EVT_JOINED. Post-join LinkCheck probe (below) already exercises MAC TX;
 * prod: 0. Increase only if a specific stack still needs settle after probe.
 */
#define LORA_POST_JOIN_MAC_SETTLE_MS 0
/** After lorawan_join()==0, verify the MAC can complete an uplink (LinkCheck
 * forced empty frame). Stops false "joined" when Zephyr signals
 * mlme_confirm_sem on non-MLME_JOIN (e.g. DevTimeReq). Pair with
 * patches/zephyr-lorawan-mlme-join-only-sem.patch on the Zephyr tree. Set
 * retries to 0 to disable (not recommended without the Zephyr patch). */
#define LORA_POST_JOIN_MAC_PROBE_RETRIES 30
#define LORA_POST_JOIN_MAC_PROBE_RETRY_MS 500
/** After this many consecutive lorawan_send() failures, clear joined and
 * schedule join backoff (re-join after LORA_JOIN_BACKOFF_HOURS). Set to 0 to
 * disable. Typical: 3. */
#define LORA_SEND_FAILURES_BEFORE_BACKOFF 3
#define LORA_BUTTON_PORT 2

/** Uplink confirmation policy (field-stable profile)
 * Confirmed: device waits for ACK in RX1/RX2; no ACK => Rx timeout (-116).
 * Unconfirmed: fire-and-forget; no ACK => no RX timeout; better for weak links.
 * Field-stable: use unconfirmed for non-critical uplinks to avoid Rx timeouts.
 * Set to 1 only where backend must acknowledge (e.g. provisioning,
 * billing-critical NFC).
 */
#define LORA_BUTTON_UPLINK_CONFIRMED                                           \
  0                                 /* public votes: unconfirmed (FRD 4.5)     \
                                     */
#define LORA_NFC_UPLINK_CONFIRMED 1 /* check-in/out/vote: confirmed */
#define LORA_HEARTBEAT_UPLINK_CONFIRMED                                        \
  0 /* battery/counter in housekeeping                                         \
     */
#define LORA_COUNTER_SYNC_CONFIRMED                                            \
  0 /* counter sync: unconfirmed to avoid Rx                                   \
       timeout treated as link loss */
/** Delay between queuing each counter-sync uplink (see counter_sync.c). 0 =
 * back-to-back; LoRa thread enforces LORA_UPLINK_MIN_INTERVAL_MS. A small
 * non-zero value (e.g. 100–200) reduces burst load on MAC/SPI-heavy builds. */
#define COUNTER_SYNC_DELAY_MS 50

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

#ifdef EPD_ENABLED
#define BUTTON_COOLDOWN_MS 15000 // was 5000, should be 15000 to sync up w/ epd update
#else
#define BUTTON_COOLDOWN_MS 5000 // NON EPD version
#endif

/* Input layer: combo scan period and session recovery (held→0 for this long =
 * reset). */
#define INPUT_COMBO_SCAN_INTERVAL_MS 50
#define INPUT_SESSION_RECOVERY_MS 250

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
  12000 /* prod: 12s; return to Normal on timeout */
#define REBOOT_LED_MS 3000

/* =============================================================================
 * LED (FRD 4.4) — on/off only; patterns are blink counts and timings
 * =============================================================================
 */
 #define NUM_LEDS 1

 /* -------------------------
  * BUTTON
  * ------------------------- */
 /* Button press accepted: quick confirmation pulse */
 #define LED_BUTTON_ACCEPTED_MS 600
 
 
 /* -------------------------
  * JOIN (LoRa / network)
  * ------------------------- */
 
 /* While joining: calm breathing (waiting / searching) */
 #define LED_JOINING_ON_MS   800
 #define LED_JOINING_OFF_MS  800
 
 /* Join success: clear celebration burst */
 #define LED_JOIN_BLINKS   4
 #define LED_JOIN_ON_MS    180
 #define LED_JOIN_OFF_MS   180
 /* After installer join: SMF waits this long so JOIN_SUCCESS LED can finish
  * before LAST_CLEANED (silent join uses 0 ms). Slightly > real burst length. */
 #define POST_JOIN_LED_BEFORE_EPD_MS 1700
 
 
 /* -------------------------
  * NFC
  * ------------------------- */
 
 /* NFC scanning: steady “ready / waiting for tag” */
 #define LED_NFC_WAITING_ON_MS   600
 #define LED_NFC_WAITING_OFF_MS  600
 
 /* NFC read fail: gentle retry signal */
 #define LED_NFC_FAIL_BLINKS  3
 #define LED_NFC_FAIL_ON_MS   160
 #define LED_NFC_FAIL_OFF_MS  240
 
 /* NFC success: clean confirmation burst */
 #define LED_CONFIRM_BLINKS  3
 #define LED_CONFIRM_ON_MS   140
 #define LED_CONFIRM_OFF_MS  140
 
 
 /* -------------------------
  * SYSTEM STATES
  * ------------------------- */
 
 /* Power on: identity blink */
 #define LED_POWER_ON_BLINKS  2
 #define LED_POWER_ON_MS      120
 #define LED_POWER_ON_OFF_MS  120
 
 /* Reboot: stable ON presence */
 #define LED_REBOOT_HOLD_MS  2000
 
 
 /* -------------------------
  * TIMING CONTROL
  * ------------------------- */
 
 /* Allow NFC / confirm burst to complete cleanly */
 #define LED_CONFIRM_SMF_BLOCK_MS  500

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
 * Offset in minutes from UTC. Written to EEPROM via downlink cmd 0x03; used only
 * when formatting timestamps for the EPD so users see local time.
 */
/** Build-time default (e.g. -480 for San Francisco PST). Used when EEPROM block
 * is uninitialized. */
#define DEFAULT_TIMEZONE_OFFSET_MINUTES (-240) // UTC-4h
#define TZ_OFFSET_MIN_MINUTES           (-1440)
#define TZ_OFFSET_MAX_MINUTES           (1440)
/* =============================================================================
 * RTC / time sync
 * =============================================================================
 */
#define RTC_SET_TIME_ON_BOOT 0
#define RTC_FORCE_SET_TIME_ON_BOOT                                             \
  0 /* prod: 0 (do not overwrite RTC from build-time) */
#define RTC_SET_YEAR 2026
#define RTC_SET_MONTH 1
#define RTC_SET_DAY 1
#define RTC_SET_HOUR 00
#define RTC_SET_MINUTE 00
#define RTC_SET_SECOND 00
#define RTC_VALID_YEAR_MIN 2026
#define RTC_GET_EPOCH_RETRIES 5
#define RTC_GET_EPOCH_RETRY_DELAY_MS 10
#define RTC_3V3A_SETTLE_MS 120
#define RTC_INIT_RETRY_COUNT 5
#define RTC_INIT_RETRY_DELAY_MS 30
#define RTC_REQUIRE_LNS_TIME_SYNC 0
#define RTC_TIME_SYNC_REQUIRED_TIMEOUT_SECONDS 30
#define LORAWAN_GPS_UTC_LEAP_SECONDS 18

/** GPS epoch (1980-01-06) to Unix epoch (1970-01-01) offset in seconds. */
#define GPS_TO_UNIX_EPOCH_OFFSET 315964800U

/** How often to poll for DeviceTimeAns after sending DeviceTimeReq (ms).
 * DeviceTimeAns arrives in RX1/RX2 (~1-2s after uplink) so poll fast.
 * prod: 2000. */
#define TIME_SYNC_POLL_INTERVAL_MS 2000

/** Default max polls per attempt when RTC_REQUIRE_LNS_TIME_SYNC=0.
 * Total wait = TIME_SYNC_POLL_INTERVAL_MS * TIME_SYNC_MAX_POLLS (~20s).
 * When RTC_REQUIRE_LNS_TIME_SYNC=1, time_sync.c overrides from
 * RTC_TIME_SYNC_REQUIRED_TIMEOUT_SECONDS. */
#define TIME_SYNC_MAX_POLLS 10

/** How many full DeviceTimeReq cycles to attempt before giving up entirely.
 * Each retry sends a fresh DeviceTimeReq on the next uplink opportunity.
 * Field-stable: 3 retries. Balance RTC sync odds vs radio load. */
#define TIME_SYNC_MAX_RETRIES 3
/** Time to wait for DeviceTimeAns after each DeviceTimeReq before retry/fail.
 */
#define TIME_SYNC_ANS_TIMEOUT_MS 12000

/** Delay (ms) between full retry cycles. Gives device time to uplink again
 * so the next DeviceTimeReq goes out in a fresh MAC frame. prod: 30000. */
#define TIME_SYNC_RETRY_DELAY_MS 30000

/** Max acceptable delta (seconds) between written and readback RTC epoch.
 * If exceeded a warning is logged. prod: 2. */
#define TIME_SYNC_RTC_READBACK_DELTA_MAX_S 2

/* Time sync is requested by smf_joined_work after all 6 counter-syncs are sent.
 * No post-join delay (DR set at join; counter sync then time sync). */

/* =============================================================================
 * Power gating (rail manager)
 * =============================================================================
 */
/** 3.3A keep-alive (ms) after last release. Covers:
 *  - deferred EEPROM flush (~5s),
 *  - EPD panel register retention so back-to-back renders hit
 *    quick-resume (~40 ms) instead of cold-start recovery (~250 ms + cold
 *    refresh waveform),
 *  - staff/NFC multi-step flows (check-in → check-out, staff-combo → device
 *    info → back to last cleaned). display_manager bumps this again on every
 *    EPD job release so keep-alive is always measured from the last render. */
#define RAIL_MANAGER_3V3A_KEEPALIVE_MS 60000

/* =============================================================================
 * Housekeeping / Heartbeat (FRD 4.9)
 * =============================================================================
 * Periodic worker runs RTC sync (DeviceTimeReq), link check, battery sample +
 * heartbeat uplink. When HEARTBEAT_USE_DEVEUI_JITTER=1, run is daily at
 * 00:00 UTC + (DevEUI[7]*256+DevEUI[6]) % 1440 minutes (FRD 4.9).
 */
/** When 1, heartbeat runs once per day at 00:00 UTC + DevEUI-based offset
 * (minutes). When 0, runs every HOUSEKEEPING_INTERVAL_SECONDS (e.g. for test).
 * prod: 1; BACKOFF TEST; 0 (heartbeat every 120s). */
#define HEARTBEAT_USE_DEVEUI_JITTER 1
/** Fallback interval (seconds) when jitter is off or RTC unavailable. */
#define HOUSEKEEPING_INTERVAL_SECONDS 86400
/** After enabling 3.3A/3.3 for battery ADC read; 0 = no wait. */
#define HOUSEKEEPING_RAIL_ADC_SETTLE_MS 100
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
#define EPD_THANKS_DISPLAY_MS 1500
/** Cleaning screen auto-revert to last cleaned if no check-out (ms).
 * prod: 45; test: 1–3. */
#define EPD_CLEANING_REVERT_MINUTES 45U
#define EPD_CLEANING_AUTO_REVERT_MS (EPD_CLEANING_REVERT_MINUTES * 60U * 1000U)
/**
 * EPD and LoRa share SPI. Button path uses display_show_thanks_sync: EPD first
 * (block until done), then LoRa/EEPROM with clear SPI for downlinks.
 *
 * DISPLAY_WORK_DELAY_MS: delay for async LAST_CLEANED / CLEANING from
 * enqueue_job (e.g. thanks_timer). DEVICE_INFO / CONNECTING / LOGO use 0 ms
 * (staff or join UI, no uplink race). SMF uses display_show_*_sync for
 * last-cleaned after Device Info timeout and device-info entry so LED/rail
 * and EPD flush stay ordered. Must be >= ~4500 ms when scheduling after
 * confirmed uplinks to reduce LoRa RX contention.
 */
#define DISPLAY_WORK_DELAY_MS 5000

/* =============================================================================
 * Thread and message queue sizing (single source of truth for prod tuning)
 * =============================================================================
 * Tune these with stack usage reports (-fstack-usage / CONFIG_STACK_SENTINEL).
 * Msgq sizes should cover burst traffic; too small => -ENOMEM, too large =>
 * RAM.
 *
 * Runtime stack high-water: enable CONFIG_THREAD_ANALYZER* in prj.conf
 * (see v1.1/app/prj.conf) and inspect logs, or use thread analyzer shell
 * commands when console is enabled.
 */
#define SMF_MSGQ_SIZE 24
#define SMF_MSGQ_ALIGN 4
/** For smf_post_event on critical events: bounded wait vs dropping (RTOS
 * backpressure). Best-effort events use K_NO_WAIT only (housekeeping tick).
 */
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
 * Firmware Version
 * =============================================================================
 */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 3
#define FW_VERSION_PATCH 0
#define FW_VERSION_STRING "1.3.0"

#define HW_VERSION_MAJOR 1
#define HW_VERSION_MINOR 4
#define HW_VERSION_PATCH 1
#define HW_VERSION_STRING "1.4.1"

#endif /* SYS_CONFIG_H */
