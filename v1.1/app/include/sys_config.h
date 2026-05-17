#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

/*
 * FlexBox v1.2 — Single point for FRD-configurable parameters.
 * Sections: LoRa, Buttons/Input, Combo & mode timeouts, LED, EEPROM, RTC/Time
 * sync. Values marked * in FRD are tunable here.
 *
 * --- Production lock-down (ensure these before release) ---
 * MAPEK_LAB_FAST_TEST         : must be 0 for release (1 = desk MAPE-K tests).
 * LORA_JOIN_BACKOFF_HOURS     : prod 1 h; MAPEK_LAB_FAST_TEST → 1 min.
 * HEARTBEAT_USE_DEVEUI_JITTER : overridden when MAPEK_LAB_FAST_TEST (120 s HK).
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
#define DEVICE_UNIT_ID_STRING "UNIT-0044"
/* END UNIT_ID (gen_euis.py) */

/* Last provisioning stamp (UTC) from onboarding/gen_euis.py (--stamp-provision-only
 * or full run). rtc.c uses DEVICE_PROVISION_UNIX_UTC when != 0 to program the RTC on
 * boot (see RTC_SET_TIME_ON_BOOT). If 0, RTC falls back to RTC_SET_YEAR/... below. */
/* BEGIN PROVISION_UTC (gen_euis.py) — do not edit by hand */
#define DEVICE_PROVISION_UNIX_UTC 1778982738ULL
#define DEVICE_PROVISION_ISO8601_UTC "2026-05-17T01:52:18Z"
/* END PROVISION_UTC (gen_euis.py) */

/* =============================================================================
 * Registry / AWS onboarding — CSV name_prefix (read by onboarding/gen_euis.py)
 * Set to "" for none. Not modified by gen_euis.py (edit by hand per deployment).
 * =============================================================================
 */
#ifndef DEVICE_REGISTRY_NAME_PREFIX_STRING
#define DEVICE_REGISTRY_NAME_PREFIX_STRING "LAX"
#endif

/* =============================================================================
 * LoRa (FRD 4.5)
 * =============================================================================
 */
/** Desk test: fast MAPE-K stale/probe/session-lost + 1 min rejoin backoff.
 * Set to 1, build, flash, run MAPE-K tests. Production release: must be 0. */
#ifndef MAPEK_LAB_FAST_TEST
#define MAPEK_LAB_FAST_TEST 0
#endif

#define LORA_MAX_PAYLOAD_SIZE 11
/** Application downlink FRMPayload max length (SMF_EVT_DOWNLINK buffer). Can be
 * larger than uplink LORA_MAX_PAYLOAD_SIZE; DR/region defines air limit (~51 B
 * common for EU868 RX2). */
#ifndef LORA_MAX_DOWNLINK_FRMPAYLOAD_SIZE
#define LORA_MAX_DOWNLINK_FRMPAYLOAD_SIZE 51
#endif
/** DL 0x99 custom text: max decoded ASCII for EPD banner (no-op if EPD off). */
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
/** Field-stable: 20s between join attempts (was 15). Reduces join-cycle load.
 */
#define LORA_JOIN_RETRY_DELAY_SECONDS 30
/** Number of join attempts in one "cycle" before assuming genuine failure (e.g.
 * no gateway). */
#define LORA_JOIN_ATTEMPTS_PER_CYCLE 20
/** After all attempts in a cycle fail, wait this many hours before next join
 * cycle (deployed device cannot be re-joined by human). Must be integer
 * (K_HOURS expects int). 0 = 1-min backoff (MAPEK_LAB_FAST_TEST); prod: 1 h.
 */
#if MAPEK_LAB_FAST_TEST
#define LORA_JOIN_BACKOFF_HOURS 0
#else
#define LORA_JOIN_BACKOFF_HOURS 1
#endif
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
/** After a successful post-join MAC probe, sleep this many ms so the LoRaWAN
 *  stack has time to deliver the LinkCheckAns in RX1/RX2 before we leave the
 *  probe and the install-screen stats are read. 0 = don't wait. Typical RX2
 *  close is ~2s after TX; 2500 covers it with margin. */
#define LORA_POST_JOIN_ANS_SETTLE_MS 2500
/** Number of EXTRA LinkCheckReqs to send after the normal post-join probe to
 *  enrich the install-screen sample. 0 = reuse post-join probe Ans only.
 *  >0 = each extra request is spaced LORA_INSTALL_EXTRA_LINK_SPACING_MS apart
 *  and waits LORA_POST_JOIN_ANS_SETTLE_MS for its Ans. Keep small: each is a
 *  real airtime burst. */
#define LORA_INSTALL_EXTRA_LINK_SAMPLES 0
#define LORA_INSTALL_EXTRA_LINK_SPACING_MS 2000
/** MAPE-K link Monitor: EWMA update uses ewma += ((sample<<8)-ewma)>>MAPEK_LINK_EWMA_SHIFT
 * (approx. alpha = 1/2^shift). 3 => ~1/8 per sample; increase for slower smoothing. */
#ifndef MAPEK_LINK_EWMA_SHIFT
#define MAPEK_LINK_EWMA_SHIFT 3U
#endif
#if MAPEK_LAB_FAST_TEST
/** MAPE-K lab profile (~8–10 min Test A: STALE → 3 Plan LC → session lost → rejoin). */
#define MAPEK_LINK_MONITOR_LOG_INTERVAL_MS 0U
#define MAPEK_PROBE_ANS_TIMEOUT_MS (30U * 1000U)
#define MAPEK_LNS_HEARD_STALE_MS (2U * 60U * 1000U)
#define MAPEK_PLAN_PROBE_COOLDOWN_MS (45U * 1000U)
#define MAPEK_PLAN_PROBE_COOLDOWN_FAIL_MS (90U * 1000U)
#define MAPEK_PLAN_FAIL_COUNT_LONG_COOLDOWN 3U
#define MAPEK_SESSION_LOST_ENABLE 1
#define MAPEK_SESSION_LOST_PROBE_FAILS 3U
#define MAPEK_PLAN_LINKCHECK_PERIOD_MS 0U
#else
/** Unused: MAPE-K logs one event line only (no periodic timer). */
#ifndef MAPEK_LINK_MONITOR_LOG_INTERVAL_MS
#define MAPEK_LINK_MONITOR_LOG_INTERVAL_MS 0U
#endif
/** Analyze: after probe TX, wait this long for heard advance before NO_ANSWER. */
#ifndef MAPEK_PROBE_ANS_TIMEOUT_MS
#define MAPEK_PROBE_ANS_TIMEOUT_MS (2U * 60U * 1000U)
#endif
/** Analyze/Plan: no DL/LinkCheckAns refresh → STALE; Plan queues LinkCheck. */
#ifndef MAPEK_LNS_HEARD_STALE_MS
#define MAPEK_LNS_HEARD_STALE_MS (6U * 60U * 60U * 1000U)
#endif
/** Plan: min ms between Plan-driven LinkCheck probes (healthy / low fail count). */
#ifndef MAPEK_PLAN_PROBE_COOLDOWN_MS
#define MAPEK_PLAN_PROBE_COOLDOWN_MS (30U * 60U * 1000U)
#endif
/** Plan: cooldown after MAPEK_PLAN_FAIL_COUNT_LONG_COOLDOWN probe failures. */
#ifndef MAPEK_PLAN_PROBE_COOLDOWN_FAIL_MS
#define MAPEK_PLAN_PROBE_COOLDOWN_FAIL_MS (2U * 60U * 60U * 1000U)
#endif
/** Plan: use long cooldown when probe_fail_count >= this. */
#ifndef MAPEK_PLAN_FAIL_COUNT_LONG_COOLDOWN
#define MAPEK_PLAN_FAIL_COUNT_LONG_COOLDOWN 3U
#endif
/** Execute: after this many Plan probe failures while STALE/DEGRADED, request
 * LoRa session lost (OTAA rejoin after LORA_JOIN_BACKOFF_HOURS). */
#ifndef MAPEK_SESSION_LOST_ENABLE
#define MAPEK_SESSION_LOST_ENABLE 1
#endif
#ifndef MAPEK_SESSION_LOST_PROBE_FAILS
#define MAPEK_SESSION_LOST_PROBE_FAILS MAPEK_PLAN_FAIL_COUNT_LONG_COOLDOWN
#endif
#ifndef MAPEK_PLAN_LINKCHECK_PERIOD_MS
#define MAPEK_PLAN_LINKCHECK_PERIOD_MS 0U
#endif
#endif /* MAPEK_LAB_FAST_TEST */
/** MAPE-K Analyze: EWMA on degradation score 0=best 255=worst (separate from Monitor path EWMA). */
#ifndef MAPEK_ANALYZE_SCORE_SMOOTH_SHIFT
#define MAPEK_ANALYZE_SCORE_SMOOTH_SHIFT 4U
#endif
#ifndef MAPEK_RF_MARGIN_EXCELLENT_MIN
#define MAPEK_RF_MARGIN_EXCELLENT_MIN 22U
#endif
#ifndef MAPEK_RF_MARGIN_GOOD_MIN
#define MAPEK_RF_MARGIN_GOOD_MIN 16U
#endif
#ifndef MAPEK_RF_MARGIN_FAIR_MIN
#define MAPEK_RF_MARGIN_FAIR_MIN 10U
#endif
#ifndef MAPEK_RF_GW_GOOD_MIN
#define MAPEK_RF_GW_GOOD_MIN 2U
#endif
#ifndef MAPEK_RF_GW_FAIR_MIN
#define MAPEK_RF_GW_FAIR_MIN 1U
#endif
/** EWMA RSSI (dBm): above = contribution to excellent side. */
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
#define LORA_NFC_UPLINK_CONFIRMED 0 /* check-in/out/vote: unconfirmed */
#define LORA_HEARTBEAT_UPLINK_CONFIRMED                                        \
  0 /* battery/counter in housekeeping                                         \
     */
#define LORA_COUNTER_SYNC_CONFIRMED                                            \
  0 /* counter sync: unconfirmed to avoid Rx                                   \
       timeout treated as link loss */
/** Delay between queuing each counter-sync uplink (see counter_sync.c). 0 =
 * back-to-back. Actual airtime spacing is dominated by LORA_UPLINK_MIN_INTERVAL_MS
 * in the LoRa thread (EU868 fair use / complements stack duty-cycle logic).
 * A non-zero value only smooths how fast the message queue fills (SPI/MAC). */
#define COUNTER_SYNC_DELAY_MS 100
/** Added to COUNTER_SYNC_DELAY_MS sleep before each counter-sync enqueue;
 * seeded from DevEUI + btn + uptime — spreads fleet bursts on the gateway.
 * Set 0 to disable jitter. Typical 400-800 ms. */
#define COUNTER_SYNC_JITTER_MAX_MS 600

/* =============================================================================
 * Buttons / Input (FRD 4.1; gpio-keys aliases in DT overlay)
 * =============================================================================
 */
#define NUM_BUTTONS 6

/* LoRa: estimate uplinks to drain before DeviceTimeReq after a burst */

/** Post-join: counter sync (+ 1 housekeeping snapshot queued after burst). */

#define LORA_BURST_JOIN_TAIL_UPLINKS ((uint32_t)NUM_BUTTONS + 1U)
/** HK: battery uplink + counter sync (+ snapshot last in housekeeping_run). */

#define LORA_BURST_HK_TAIL_UPLINKS ((uint32_t)NUM_BUTTONS + 2U)

/** Slack: lorawan retries, MAC interleave, worst-case counter-sync jitter. */

#define LORA_BURST_TIME_SYNC_TAIL_FUDGE_MS 5000U

#define LORA_BURST_COUNTER_JITTER_BUDGET_MS                                                                          \
  ((uint32_t)NUM_BUTTONS * (uint32_t)COUNTER_SYNC_JITTER_MAX_MS)

#define LORA_POST_COUNTER_BURST_TIME_SYNC_DELAY_JOIN_MS                                                              \
  (((uint32_t)LORA_BURST_JOIN_TAIL_UPLINKS * (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS) +                                \
   (uint32_t)LORA_BURST_TIME_SYNC_TAIL_FUDGE_MS + (uint32_t)LORA_BURST_COUNTER_JITTER_BUDGET_MS)

#define LORA_POST_COUNTER_BURST_TIME_SYNC_DELAY_HK_MS                                                                \
  (((uint32_t)LORA_BURST_HK_TAIL_UPLINKS * (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS) +                                  \
   (uint32_t)LORA_BURST_TIME_SYNC_TAIL_FUDGE_MS + (uint32_t)LORA_BURST_COUNTER_JITTER_BUDGET_MS)

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
#define COMBO_STAFF_HOLD_MS 1000       /* 0+1: enter Staff */
#define COMBO_DEVICE_INFO_HOLD_MS 2000 /* 0+1+5: Device Info */
#define COMBO_JOIN_HOLD_MS 2000        /* 0+1+2 in Staff: deliberate join */
#define COMBO_REBOOT_HOLD_MS 5000      /* 0+1+2+3 in Staff: reboot */

/* =============================================================================
 * Mode timeouts (FRD 3.3, 3.4) — return to Normal when elapsed
 * =============================================================================
 */
/* Staff: 20s (longer than FRD 10s* to allow Reboot combo 0+1+2+3 hold 10s). */
#define STAFF_TIMEOUT_MS 20000
#define DEVICE_INFO_TIMEOUT_MS                                                 \
  20000 /* prod: 20s; return to Normal on timeout */
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
 #define LED_POWER_ON_BLINKS  3
 #define LED_POWER_ON_MS      120
 #define LED_POWER_ON_OFF_MS  120
 
 /* Reboot: stable ON presence */
 #define LED_REBOOT_HOLD_MS  3000
 
 
 /* -------------------------
  * TIMING CONTROL
  * ------------------------- */
 
 /* Allow NFC / confirm burst to complete cleanly */
 #define LED_CONFIRM_SMF_BLOCK_MS  1000

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
#define DEFAULT_TIMEZONE_OFFSET_MINUTES (120) // UTC+2h
#define TZ_OFFSET_MIN_MINUTES           (-1440)
#define TZ_OFFSET_MAX_MINUTES           (1440)
/* =============================================================================
 * RTC / time sync
 * =============================================================================
 */
#define RTC_SET_TIME_ON_BOOT 1
#define RTC_FORCE_SET_TIME_ON_BOOT                                             \
  0 /* prod: 0 (do not overwrite RTC from build-time) */
/* Fallback calendar time when DEVICE_PROVISION_UNIX_UTC is 0 (see rtc.c). */
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

/* DeviceTimeReq is scheduled by lora_schedule_time_sync_after_counter_burst()
 * after counter (+ snapshot/HK burst) draining — see TIME_SYNC_BURST timing
 * macros. ADR is enabled after OTAA succeeds so LinkADRReq can converge DR
 * during that burst before device-time is requested. */

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

/** 3.6V keep-alive (ms) after last release. Holds PN5180 powered between
 *  back-to-back NFC scans (e.g. staff check-in → check-out) so the worker can
 *  skip pn5180_init+configure (~150 ms power settle + ~40-80 ms chip init) and
 *  start the inventory loop immediately. Set to 0 to disable keep-alive and
 *  cut 3.6V the moment ref-count hits 0 (saves a few mA·s per scan, loses the
 *  warm-path latency win). */
#define RAIL_MANAGER_3V6_KEEPALIVE_MS 15000

/* =============================================================================
 * Housekeeping / Heartbeat (FRD 4.9)
 * =============================================================================
 * Periodic worker runs RTC sync (DeviceTimeReq), link check, battery sample +
 * heartbeat uplink. When HEARTBEAT_USE_DEVEUI_JITTER=1, run is daily at
 * 00:00 UTC + (DevEUI[7]*256+DevEUI[6]) % 1440 minutes (FRD 4.9).
 */
/** When 1, heartbeat runs once per day at 00:00 UTC + DevEUI-based offset
 * (minutes). When 0, runs every HOUSEKEEPING_INTERVAL_SECONDS (e.g. for test).
 * MAPEK_LAB_FAST_TEST: 0 (HK every 120 s). Prod: 1. */
#if MAPEK_LAB_FAST_TEST
#define HEARTBEAT_USE_DEVEUI_JITTER 0
#else
#define HEARTBEAT_USE_DEVEUI_JITTER 1
#endif
/** Fallback interval (seconds) when jitter is off or RTC unavailable. */
#if MAPEK_LAB_FAST_TEST
#define HOUSEKEEPING_INTERVAL_SECONDS 120
#else
#define HOUSEKEEPING_INTERVAL_SECONDS 86400
#endif
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
/** Phase 1: if no tag has been inventoried yet, give up after this many ms
 *  from scan start (faster exit when nothing is presented). */
#define NFC_SCAN_PHASE1_MS 6000
/** Absolute cap from scan start (ms). SMF NFC-mode timer uses this value; the
 *  worker extends its deadline up to (scan_t0 + NFC_SCAN_TOTAL_MS) after the
 *  first successful inventory so a weak ISO15693 coupling can still finish
 *  read_block without dropping out at phase 1. */
#define NFC_SCAN_TOTAL_MS 12000
/** Settle (ms) after cold 3.6V bring-up pulse before PN5180 init. Match boot
 * margin (~1 s rail on in main) — 500 ms was too short after long power-off. */
#define NFC_POWER_SETTLE_MS 1000
/** 3.6V off time (ms) before cold bring-up settle (clears wedged BUSY). */
#define NFC_COLD_BOOT_OFF_MS 50
/** Inventory/read retry cadence inside the scan loop (ms). */
#define NFC_POLL_INTERVAL_MS 100

/** Max attempts for pn5180_init + pn5180_configure before giving up and
 *  posting a fault. Attempt 1 is a normal init after standard settle;
 *  attempts 2..N cycle the 3.6V rail to clear a wedged chip state. */
#define NFC_INIT_MAX_ATTEMPTS 3
/** Duration (ms) 3.6V is held off during recovery cycle (attempt 2). */
#define NFC_RECOVERY_OFF_MS 50
/** Settle (ms) after re-enabling 3.6V during recovery cycle (attempt 2). */
#define NFC_RECOVERY_SETTLE_MS 300
/** Escalated off duration (ms) on attempt 3+ — longer drain to fully clear
 *  any residual charge on the chip supply. */
#define NFC_RECOVERY_OFF_MS_ESCALATED 200
/** Escalated settle (ms) after re-enabling on attempt 3+. */
#define NFC_RECOVERY_SETTLE_MS_ESCALATED 500

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

/*
 * -----------------------------------------------------------------------------
 * EPD on-screen copy (locale / market)
 * -----------------------------------------------------------------------------
 * Built into the firmware at compile time. Change these for the region you are
 * provisioning, then rebuild + flash.
 *
 * Full-screen bitmaps (boot / thanks / cleaning): set EPD_LOCALE_FR_BITMAPS.
 * 0 = English (bootLogo.c, thanks_en.c, cleaning_en.c).
 * 1 = French (boot_screen.c, thanks_fr.c, cleaning_fr.c).
 * LAST_CLEANED headline and other strings below still apply.
 * -----------------------------------------------------------------------------
 */
#if EPD_ENABLED
/** Full-screen EPD bitmaps: 0 = EN assets, 1 = FR assets (compile-time). */
#ifndef EPD_LOCALE_FR_BITMAPS
#define EPD_LOCALE_FR_BITMAPS 0
#endif

/** Main customer screen title above the last-cleaned timestamp. */

#if EPD_LOCALE_FR_BITMAPS
#define EPD_TEXT_LAST_CLEANED_HEADLINE "DERNIER NETTOYAGE"
#else
#define EPD_TEXT_LAST_CLEANED_HEADLINE "LAST CLEANED"
#endif

/** LoRa join in progress (shown before CONNECTED). */
#define EPD_TEXT_CONNECTING "Connecting..."
/** Product name on Device Info + Install header row (same string by default). */
#define EPD_TEXT_BRAND_TITLE "flexbox"
/** Footer line under Device Info. */
#define EPD_TEXT_MANUFACTURER "quire.tech"
/** Prefix before FW_VERSION_STRING on Device Info (refresh appends version). */
#define EPD_TEXT_FW_PREFIX "fw  "
#endif /* EPD_ENABLED */

/* =============================================================================
 * Device status screen — join + link check + counters
 * =============================================================================
 * Staff 0+1+5 always uses display_show_device_status_sync() when EPD_ENABLED.
 *
 * Commission boot auto-show (first JOIN / JOIN_FAIL on power-on / reset pin /
 * deliberate reboot): set EPD_DEVICE_STATUS_COMMISSION_BOOT to 1. When 0, SMF
 * goes straight to last-cleaned + counter sync (same as watchdog/brownout boot).
 */
#ifndef EPD_DEVICE_STATUS_COMMISSION_BOOT
#define EPD_DEVICE_STATUS_COMMISSION_BOOT 1
#endif

#if EPD_DEVICE_STATUS_COMMISSION_BOOT
#define EPD_DEVICE_STATUS_COMMISSION_MS 15000
#endif

/* Link tiers use post-join LinkCheckAns in lora_link_stats (no on-screen re-probe). */
/** Demod margin (dB) thresholds for 4-tier link label (lowercase on EPD). */
#define EPD_LINK_MARGIN_EXCELLENT_DB 20
#define EPD_LINK_MARGIN_GOOD_DB 10
#define EPD_LINK_MARGIN_FAIR_DB 3
/* Below FAIR threshold => weak */

#define EPD_STATUS_LABEL_LINK "link:"
#define EPD_STATUS_LABEL_GATEWAYS "gateway(s):"
#define EPD_STATUS_LABEL_MARGIN "margin:"
#define EPD_STATUS_GATEWAYS_EMPTY "gateway(s): --"
#define EPD_STATUS_MARGIN_EMPTY "margin: --"
#define EPD_STATUS_LABEL_STATUS "status:"
#define EPD_STATUS_LINK_EXCELLENT "excellent"
#define EPD_STATUS_LINK_GOOD "good"
#define EPD_STATUS_LINK_FAIR "fair"
#define EPD_STATUS_LINK_WEAK "weak"
#define EPD_STATUS_LINK_NO_RESPONSE "no response"
#define EPD_STATUS_JOINED "joined"
#define EPD_STATUS_NOT_JOINED "not joined"
#define EPD_STATUS_VALUE_NONE "--"
#define EPD_STATUS_MARGIN_SUFFIX " db"
#define EPD_STATUS_FW_PREFIX "fw: "
/**
 * EPD and LoRa share SPI. Button path uses display_show_thanks_sync: EPD first
 * (block until done), then LoRa/EEPROM with clear SPI for downlinks.
 *
 * DISPLAY_WORK_DELAY_MS: delay for async LAST_CLEANED / CLEANING from
 * enqueue_job (e.g. thanks_timer). DEVICE_STATUS / CONNECTING / LOGO use 0 ms
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
#define FW_VERSION_MINOR 4
#define FW_VERSION_PATCH 0
#define FW_VERSION_STRING "1.4.0"

#define HW_VERSION_MAJOR 1
#define HW_VERSION_MINOR 4
#define HW_VERSION_PATCH 1
#define HW_VERSION_STRING "1.4.1"

#endif /* SYS_CONFIG_H */
