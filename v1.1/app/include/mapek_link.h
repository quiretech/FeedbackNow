/**
 * MAPE-K link layer — Monitor → Analyze → Plan → Execute.
 *
 * Plan queues LinkCheck when STALE/DEGRADED/never-heard. Execute posts
 * lora_request_session_lost() after MAPEK_SESSION_LOST_PROBE_FAILS Plan probe
 * failures. Join/post-join probes use MAPEK_PROBE_SOURCE_JOIN; HK does not LC.
 */
#ifndef MAPEK_LINK_H
#define MAPEK_LINK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  MAPEK_LNS_HEARD_NONE = 0,
  MAPEK_LNS_HEARD_DL = 1,
  MAPEK_LNS_HEARD_LINK_CHECK = 2,
} mapek_lns_heard_kind_t;

typedef enum {
  MAPEK_PROBE_SOURCE_UNKNOWN = 0,
  MAPEK_PROBE_SOURCE_JOIN = 1,
  MAPEK_PROBE_SOURCE_PLAN = 2,
  MAPEK_PROBE_SOURCE_OTHER = 3,
} mapek_probe_source_t;

typedef enum {
  MAPEK_PROBE_OUTCOME_NONE = 0,
  MAPEK_PROBE_OUTCOME_PENDING = 1,
  MAPEK_PROBE_OUTCOME_SUCCESS = 2,
  MAPEK_PROBE_OUTCOME_NO_ANSWER = 3,
  MAPEK_PROBE_OUTCOME_TX_FAIL = 4,
} mapek_probe_outcome_t;

/** Analyze output consumed by Plan. */
typedef enum {
  MAPEK_LINK_STATE_OK = 0,
  MAPEK_LINK_STATE_STALE = 1,
  MAPEK_LINK_STATE_PROBE_PENDING = 2,
  MAPEK_LINK_STATE_DEGRADED = 3,
} mapek_link_state_t;

typedef enum {
  MAPEK_PLAN_INTENT_NONE = 0,
  MAPEK_PLAN_INTENT_PROBE_LC = 1,
} mapek_plan_intent_t;

typedef enum {
  MAPEK_LINK_RF_UNKNOWN = 0,
  MAPEK_LINK_RF_EXCELLENT = 1,
  MAPEK_LINK_RF_GOOD = 2,
  MAPEK_LINK_RF_FAIR = 3,
  MAPEK_LINK_RF_POOR = 4,
} mapek_link_rf_state_t;

typedef struct {
  bool joined;
  uint32_t join_transition_uptime_ms;

  uint32_t last_lns_heard_uptime_ms;
  uint8_t last_lns_heard_kind;

  bool dl_rf_have;
  int16_t last_dl_rssi;
  int8_t last_dl_snr;
  int16_t ewma_dl_rssi;
  int8_t ewma_dl_snr;

  bool lc_rf_have;
  uint8_t last_lc_margin_db;
  uint8_t last_lc_nb_gw;
  uint32_t last_lc_ans_uptime_ms;

  bool probe_armed;
  uint32_t probe_sent_uptime_ms;
  uint32_t probe_baseline_heard_ms;
  bool probe_tx_ok;
  bool probe_rx_lc_ok;
  bool probe_rx_lns_ok;
  uint8_t probe_source;
  uint32_t probe_arm_count;
} mapek_link_monitor_snapshot_t;

#define MAPEK_ANALYZE_REASON_NEVER_HEARD   (1u << 0)
#define MAPEK_ANALYZE_REASON_WEAK_DL_RSSI  (1u << 1)
#define MAPEK_ANALYZE_REASON_WEAK_DL_SNR   (1u << 2)
#define MAPEK_ANALYZE_REASON_NO_DL_RSSI    (1u << 3)
#define MAPEK_ANALYZE_REASON_PROBE_PENDING (1u << 4)
#define MAPEK_ANALYZE_REASON_PROBE_FAIL    (1u << 5)
#define MAPEK_ANALYZE_REASON_STALE_HEARD    (1u << 6)

typedef struct {
  bool session_joined;
  mapek_link_state_t link_state;
  bool lns_heard;
  uint32_t lns_heard_age_ms;
  int16_t ewma_dl_rssi;
  int8_t ewma_dl_snr;
  mapek_link_rf_state_t rf_state;
  uint32_t reasons;

  mapek_probe_outcome_t probe_outcome;
  uint32_t probe_age_ms;
  bool probe_tx_ok;
  bool probe_rx_lc_ok;
  bool probe_rx_lns_ok;

  /** Rolling LinkCheck probe failures (Knowledge; reset on SUCCESS). */
  uint16_t probe_fail_count;
} mapek_link_analyze_snapshot_t;

#define MAPEK_DL_FEED_APP_PAYLOAD       ((uint8_t)(1u << 0))
#define MAPEK_DL_FEED_LORAWAN_TIME_UPD ((uint8_t)(1u << 1))

void mapek_link_init(void);
void mapek_link_step(void);

void mapek_link_feed_link_check(uint8_t demod_margin_db, uint8_t nb_gateways);
void mapek_link_feed_dl(int16_t rssi, int8_t snr, uint8_t feed_flags);

void mapek_link_tag_next_probe_source(uint8_t source);
void mapek_link_feed_probe_begin(uint8_t source);
void mapek_link_feed_probe_tx(int tx_ret);
void mapek_link_feed_probe_armed(int tx_ret, uint8_t source);

void mapek_link_feed_join(bool joined);

bool mapek_link_monitor_get(mapek_link_monitor_snapshot_t *out);
bool mapek_link_analyze_get(mapek_link_analyze_snapshot_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MAPEK_LINK_H */
