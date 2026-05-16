/**
 * MAPE-K link layer — Monitor stage (telemetry + EWMA). Analyze/Plan/Execute TBD.
 *
 * Feeds from MAC/stack callbacks use try-lock only (non-blocking). Join feeds run
 * from the LoRa thread. Call mapek_link_step() from the LoRa thread each poll cycle.
 *
 * Analyze: RF-only link_state + separate session_joined (MAC). Monitor `joined`
 * is updated only from the LoRa thread via mapek_link_feed_join — Analyze never
 * writes join state; join backoff remains owned by lora_thread (Plan does not clear joined).
 *
 * Plan: queues immediate LinkCheckReq via `lora_request_link_check(true)` on a timer cadence
 * while session_joined (MAPEK_PLAN_LINKCHECK_PERIOD_MS in sys_config.h).
 *
 * Roadmap: consolidated log line + Plan/Execute getters for richer intents.
 */
#ifndef MAPEK_LINK_H
#define MAPEK_LINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Zephyr LoRaWAN MCPS / MAC negative errno commonly seen when the stack waited
 * for RX2 (e.g. confirmed ACK not received): "McpsRequest failed : Rx 2 timeout".
 * Still worth logging in Monitor when only some uplinks stay confirmed (e.g. NFC).
 */
#ifndef MAPEK_LORAWAN_MCPS_RX2_TIMEOUT_ERRNO
#define MAPEK_LORAWAN_MCPS_RX2_TIMEOUT_ERRNO (-116)
#endif

/** Bucket for Analyze / dashboards (stable numeric codes). */
typedef enum {
  MAPEK_MCPS_CLASS_OK = 0,
  MAPEK_MCPS_CLASS_RX_TIMEOUT = 1,
  MAPEK_MCPS_CLASS_ENOTCONN = 2,
  MAPEK_MCPS_CLASS_BUSY = 3,
  MAPEK_MCPS_CLASS_INVAL = 4,
  MAPEK_MCPS_CLASS_OTHER = 5,
} mapek_mcps_class_t;

typedef enum {
  MAPEK_UL_MCPS_APP = 0,
  MAPEK_UL_MCPS_LINK_CHECK = 1,
} mapek_ul_mcps_kind_t;

/** Snapshot of Monitor state (thread-safe copy via mapek_link_monitor_get). */
typedef struct {
  bool joined;
  uint32_t join_transition_uptime_ms;

  bool linkcheck_have_sample;
  uint8_t last_margin_db;
  uint8_t last_nb_gateways;
  uint32_t last_linkcheck_uptime_ms;
  /** EWMA of margin (approx. integer dB; internal Q8 rounded). */
  uint8_t ewma_margin_db;
  /** EWMA of gateway count (rounded). */
  uint8_t ewma_nb_gateways;

  bool dl_have_sample;
  int16_t last_dl_rssi;
  int8_t last_dl_snr;
  uint32_t last_dl_uptime_ms;
  bool last_dl_app_payload;
  /** MAC/DeviceTimeAns path sets this when LORAWAN_TIME_UPDATED was set on that DL. */
  bool last_dl_lorawan_time_updated;
  int16_t ewma_dl_rssi;
  int8_t ewma_dl_snr;

  /** Last lorawan_send / MCPS-style result (raw errno). */
  int16_t mcps_last_errno;
  uint8_t mcps_last_class;
  uint8_t mcps_last_ul_kind;
  uint8_t mcps_last_port;
  uint8_t mcps_last_len;
  bool mcps_last_confirmed;
  uint32_t mcps_last_uptime_ms;

  uint32_t mcps_app_ok_count;
  uint32_t mcps_app_fail_count;
  uint32_t mcps_lc_tx_ok_count;
  uint32_t mcps_lc_tx_fail_count;

  /** True after successful lorawan_request_link_check until LinkCheckAns fed. */
  bool linkcheck_req_pending;
  uint32_t linkcheck_req_sent_uptime_ms;
} mapek_link_monitor_snapshot_t;

/** RF link quality only (not MAC join). UNKNOWN = no LC/DL telemetry yet. */
typedef enum {
  MAPEK_LINK_RF_UNKNOWN = 0,
  MAPEK_LINK_RF_EXCELLENT = 1,
  MAPEK_LINK_RF_GOOD = 2,
  MAPEK_LINK_RF_FAIR = 3,
  MAPEK_LINK_RF_POOR = 4,
} mapek_link_rf_state_t;

/** Analyze reason bits (why we degraded / UNKNOWN). */
#define MAPEK_ANALYZE_REASON_LOW_MARGIN        (1u << 0)
#define MAPEK_ANALYZE_REASON_LOW_GW            (1u << 1)
#define MAPEK_ANALYZE_REASON_LC_PENDING_STALE  (1u << 2)
#define MAPEK_ANALYZE_REASON_WEAK_DL_RSSI        (1u << 3)
#define MAPEK_ANALYZE_REASON_WEAK_DL_SNR       (1u << 4)
#define MAPEK_ANALYZE_REASON_NO_LINKCHECK      (1u << 5)
#define MAPEK_ANALYZE_REASON_NO_DL             (1u << 6)
#define MAPEK_ANALYZE_REASON_MCPS_RX_TIMEOUT   (1u << 7)

typedef struct {
  mapek_link_rf_state_t rf_state;
  /** Same as Monitor joined; MAC/session — not folded into rf_state. */
  bool session_joined;
  uint32_t reasons;
  /** Instant degradation 0=best 255=worst (pre-smooth). */
  uint8_t degradation_raw;
  /** Smoothed degradation mapped to rf_state when telemetry exists. */
  uint8_t degradation_smoothed;
} mapek_link_analyze_snapshot_t;

/** Bitmask for mapek_link_feed_dl(). */
#define MAPEK_DL_FEED_APP_PAYLOAD       ((uint8_t)(1u << 0))
#define MAPEK_DL_FEED_LORAWAN_TIME_UPD ((uint8_t)(1u << 1))

void mapek_link_init(void);

/**
 * LoRa thread: call once per message-loop iteration (after handling cmd/uplink).
 */
void mapek_link_step(void);

/** MAC LinkCheckAns context: non-blocking feed. */
void mapek_link_feed_link_check(uint8_t demod_margin_db, uint8_t nb_gateways);

/**
 * LoRa thread only: MCPS outcome after lorawan_send or lorawan_request_link_check.
 * @param ul_kind MAPEK_UL_MCPS_APP (FRMPayload) or MAPEK_UL_MCPS_LINK_CHECK.
 */
void mapek_link_feed_mcps_uplink(int mcps_ret, uint8_t ul_kind, uint8_t port,
                                 uint8_t len, bool confirmed);

/**
 * LoRaWAN downlink callback context: non-blocking feed.
 * @param feed_flags MAPEK_DL_FEED_* : app FRMPayload vs MAC-only, DeviceTimeAns (tu).
 */
void mapek_link_feed_dl(int16_t rssi, int8_t snr, uint8_t feed_flags);

/** LoRa thread only: join/session edge (logs on change). */
void mapek_link_feed_join(bool joined);

/** Copy Monitor snapshot; safe from any thread. */
bool mapek_link_monitor_get(mapek_link_monitor_snapshot_t *out);

/** Last Analyze pass (updated in mapek_link_step + periodic log). Thread-safe copy. */
bool mapek_link_analyze_get(mapek_link_analyze_snapshot_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MAPEK_LINK_H */
