/**
 * MAPE-K inter-phase contracts — enums and structs only.
 */
#ifndef MAPEK_TYPES_H
#define MAPEK_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  LINK_OK = 0,
  LINK_STALE,
  LINK_DEGRADED,
  LINK_SUSPECT_SESSION,
  LINK_SUSPECT_DELETED,
} link_state_t;

typedef enum {
  HYPOTHESIS_UNKNOWN = 0,
  HYPOTHESIS_RF_ISSUE,
  HYPOTHESIS_BACKHAUL_OUTAGE,
  HYPOTHESIS_SESSION_CORRUPT,
  HYPOTHESIS_DELETED,
} failure_hypothesis_t;

typedef enum {
  RF_UNKNOWN = 0,
  RF_EXCELLENT,
  RF_GOOD,
  RF_FAIR,
  RF_POOR,
} rf_quality_t;

typedef enum {
  HB_NOT_FIRED = 0,
  HB_PENDING,
  HB_SUCCESS,
  HB_NO_ANSWER,
  HB_TX_FAIL,
} heartbeat_result_t;

typedef enum {
  INTENT_NONE = 0,
  INTENT_FIRE_HEARTBEAT,
  INTENT_REJOIN,
  INTENT_REJOIN_SLOW,
} plan_intent_t;

#define MAPEK_REASON_NEVER_HEARD (1u << 0)
#define MAPEK_REASON_STALE (1u << 1)
#define MAPEK_REASON_HB_NO_ANSWER (1u << 2)
#define MAPEK_REASON_HB_TX_FAIL (1u << 3)
#define MAPEK_REASON_WEAK_RSSI (1u << 4)
#define MAPEK_REASON_WEAK_SNR (1u << 5)
#define MAPEK_REASON_BACKHAUL_WAIT (1u << 6)
#define MAPEK_REASON_SESSION_SUSPECT (1u << 7)
#define MAPEK_REASON_DELETED_SUSPECT (1u << 8)

typedef struct {
  bool joined;
  bool lns_ever_heard;
  uint32_t lns_heard_age_ms;

  bool dl_rf_valid;
  int16_t ewma_rssi;
  int8_t ewma_snr;

  heartbeat_result_t hb_result;
  uint32_t hb_last_ms;
  uint8_t hb_lc_margin_db;
  uint8_t hb_lc_nb_gw;

  uint32_t now_ms;
} mon_snap_t;

typedef struct {
  link_state_t link_state;
  failure_hypothesis_t hypothesis;
  rf_quality_t rf_quality;
  uint32_t degraded_duration_ms;
  uint32_t reasons;
  bool uplink_allowed;

  /** Diagnosis strength 0–100 (separate from action). */
  uint8_t confidence;
  /** Action urgency 0–100; Plan gates OTAA on this. */
  uint8_t urgency;

  /** Raw F2/F3 scores from probabilistic classifier (logging). */
  uint16_t f2_score;
  uint16_t f3_score;
} analyze_out_t;

typedef struct {
  plan_intent_t intent;
  uint32_t jitter_ms;
  bool hb_due;
} plan_out_t;

#endif /* MAPEK_TYPES_H */
