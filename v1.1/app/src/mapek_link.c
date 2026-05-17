/**
 * MAPE-K: Monitor → Analyze → Plan (LinkCheck) → Execute (session lost → LoRa cmd).
 */
#include "mapek_link.h"

#include "lora_app.h"
#include "sys_config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(mapek_link, CONFIG_LOG_DEFAULT_LEVEL);

static K_MUTEX_DEFINE(mon_mutex);

static int32_t ewma_rssi_q8 = INT32_MIN;
static int32_t ewma_snr_q8 = INT32_MIN;

static bool mon_joined;
static uint32_t mon_join_transition_ms;

static uint32_t last_lns_heard_ms;
static uint8_t last_lns_heard_kind;

static bool dl_rf_have;
static int16_t dl_last_rssi;
static int8_t dl_last_snr;

static bool lc_rf_have;
static uint8_t lc_last_margin;
static uint8_t lc_last_gw;
static uint32_t lc_last_ans_ms;

static bool probe_armed;
static uint32_t probe_sent_ms;
static uint32_t probe_baseline_heard_ms;
static bool probe_tx_ok;
static bool probe_rx_lc_ok;
static bool probe_rx_lns_ok;
static uint8_t probe_source;
static uint32_t probe_arm_count;

static mapek_link_analyze_snapshot_t an_last;

/** Knowledge (Plan / Execute memory). */
static uint16_t kn_probe_fail_count;
static uint16_t kn_plan_probe_fail_count;
static uint32_t kn_last_plan_probe_ms;
static uint32_t plan_last_linkcheck_req_ms;
static uint8_t kn_next_probe_source;
static bool kn_session_lost_posted;
static mapek_probe_outcome_t kn_last_terminal_outcome;

static mapek_link_state_t log_prev_link_state;

/** Edge detector for terminal probe outcomes; reset on each new probe arm. */
static mapek_probe_outcome_t analyze_prev_outcome;

static void analyze_reset_locked(void);
static void analyze_run_locked(void);
static void plan_execute_locked(void);
static void probe_disarm_locked(void);
static uint32_t mon_heard_age_ms_locked(uint32_t now);
static void mapek_log_locked(const char *ev);
static void mapek_log_wrn_locked(const char *ev);

static const char *mapek_st_str(mapek_link_state_t st) {
  switch (st) {
  case MAPEK_LINK_STATE_STALE:
    return "STALE";
  case MAPEK_LINK_STATE_PROBE_PENDING:
    return "PENDING";
  case MAPEK_LINK_STATE_DEGRADED:
    return "DEGRADED";
  default:
    return "OK";
  }
}

static const char *mapek_probe_outcome_str(mapek_probe_outcome_t po) {
  switch (po) {
  case MAPEK_PROBE_OUTCOME_PENDING:
    return "PENDING";
  case MAPEK_PROBE_OUTCOME_SUCCESS:
    return "OK";
  case MAPEK_PROBE_OUTCOME_NO_ANSWER:
    return "NO_ANSWER";
  case MAPEK_PROBE_OUTCOME_TX_FAIL:
    return "TX_FAIL";
  default:
    return "idle";
  }
}

static const char *mapek_probe_src_str(uint8_t src) {
  switch (src) {
  case MAPEK_PROBE_SOURCE_JOIN:
    return "JOIN";
  case MAPEK_PROBE_SOURCE_PLAN:
    return "PLAN";
  case MAPEK_PROBE_SOURCE_OTHER:
    return "OTHER";
  default:
    return "?";
  }
}

static void mapek_log_locked(const char *ev) {
  const uint32_t now = k_uptime_get_32();
  const char *probe_s = mapek_probe_outcome_str(an_last.probe_outcome);

  if (ev == NULL) {
    return;
  }

  if (last_lns_heard_ms == 0U) {
    LOG_INF("mapek: ev %s j=%u st=%s heard=never pf=%u/%u probe=%s",
            ev, (unsigned)mon_joined, mapek_st_str(an_last.link_state),
            (unsigned)kn_plan_probe_fail_count,
            (unsigned)MAPEK_SESSION_LOST_PROBE_FAILS, probe_s);
  } else {
    LOG_INF("mapek: ev %s j=%u st=%s heard=%us pf=%u/%u probe=%s",
            ev, (unsigned)mon_joined, mapek_st_str(an_last.link_state),
            (unsigned)(mon_heard_age_ms_locked(now) / 1000U),
            (unsigned)kn_plan_probe_fail_count,
            (unsigned)MAPEK_SESSION_LOST_PROBE_FAILS, probe_s);
  }
}

static void mapek_log_wrn_locked(const char *ev) {
  const uint32_t now = k_uptime_get_32();
  const char *probe_s = mapek_probe_outcome_str(an_last.probe_outcome);

  if (ev == NULL) {
    return;
  }

  if (last_lns_heard_ms == 0U) {
    LOG_WRN("mapek: ev %s j=%u st=%s heard=never pf=%u/%u probe=%s",
            ev, (unsigned)mon_joined, mapek_st_str(an_last.link_state),
            (unsigned)kn_plan_probe_fail_count,
            (unsigned)MAPEK_SESSION_LOST_PROBE_FAILS, probe_s);
  } else {
    LOG_WRN("mapek: ev %s j=%u st=%s heard=%us pf=%u/%u probe=%s",
            ev, (unsigned)mon_joined, mapek_st_str(an_last.link_state),
            (unsigned)(mon_heard_age_ms_locked(now) / 1000U),
            (unsigned)kn_plan_probe_fail_count,
            (unsigned)MAPEK_SESSION_LOST_PROBE_FAILS, probe_s);
  }
}

static void ewma_i16_q8(int32_t *state_q8, int16_t sample) {
  const unsigned int sh = (unsigned int)MAPEK_LINK_EWMA_SHIFT;
  int32_t x = (int32_t)sample << 8;
  if (*state_q8 == INT32_MIN) {
    *state_q8 = x;
  } else {
    *state_q8 += (x - *state_q8) >> sh;
  }
}

static void ewma_i8_q8(int32_t *state_q8, int8_t sample) {
  ewma_i16_q8(state_q8, (int16_t)sample);
}

static void mon_mark_lns_heard_locked(uint8_t kind) {
  last_lns_heard_ms = k_uptime_get_32();
  last_lns_heard_kind = kind;
}

static bool mon_dl_ewma_ready_locked(void) {
  return ewma_rssi_q8 != INT32_MIN && ewma_snr_q8 != INT32_MIN;
}

static mapek_link_rf_state_t rf_from_dl_ewma_locked(void) {
  const int16_t er = (int16_t)(ewma_rssi_q8 >> 8);
  const int8_t es = (int8_t)(ewma_snr_q8 >> 8);

  if (er <= MAPEK_RF_RSSI_POOR_DB || es < MAPEK_RF_SNR_POOR_MIN) {
    return MAPEK_LINK_RF_POOR;
  }
  if (er <= MAPEK_RF_RSSI_FAIR_DB || es < MAPEK_RF_SNR_FAIR_MIN) {
    return MAPEK_LINK_RF_FAIR;
  }
  if (er <= MAPEK_RF_RSSI_GOOD_DB || es < MAPEK_RF_SNR_GOOD_MIN) {
    return MAPEK_LINK_RF_GOOD;
  }
  return MAPEK_LINK_RF_EXCELLENT;
}

static bool mon_probe_heard_advanced_locked(void) {
  if (!probe_armed || last_lns_heard_ms == 0U) {
    return false;
  }
  if (last_lns_heard_ms > probe_baseline_heard_ms) {
    return true;
  }
  if (last_lns_heard_ms == probe_baseline_heard_ms && probe_sent_ms != 0U &&
      last_lns_heard_ms >= probe_sent_ms) {
    return true;
  }
  return false;
}

static void mon_probe_note_lns_rx_locked(void) {
  if (!probe_armed) {
    return;
  }
  if (mon_probe_heard_advanced_locked()) {
    probe_rx_lns_ok = true;
  }
}

static mapek_probe_outcome_t analyze_probe_outcome_locked(uint32_t now) {
  if (!probe_armed) {
    return MAPEK_PROBE_OUTCOME_NONE;
  }
  if (!probe_tx_ok) {
    return MAPEK_PROBE_OUTCOME_TX_FAIL;
  }
  if (probe_rx_lns_ok || probe_rx_lc_ok || mon_probe_heard_advanced_locked()) {
    return MAPEK_PROBE_OUTCOME_SUCCESS;
  }
  if (probe_sent_ms != 0U &&
      (now - probe_sent_ms) >= MAPEK_PROBE_ANS_TIMEOUT_MS) {
    return MAPEK_PROBE_OUTCOME_NO_ANSWER;
  }
  return MAPEK_PROBE_OUTCOME_PENDING;
}

static void probe_disarm_locked(void) {
  probe_armed = false;
  probe_sent_ms = 0U;
  probe_baseline_heard_ms = 0U;
  probe_tx_ok = false;
  probe_rx_lc_ok = false;
  probe_rx_lns_ok = false;
  probe_source = (uint8_t)MAPEK_PROBE_SOURCE_UNKNOWN;
}

static void knowledge_on_probe_terminal_locked(mapek_probe_outcome_t outcome) {
  const uint8_t src = probe_source;

  if (outcome == MAPEK_PROBE_OUTCOME_SUCCESS) {
    kn_probe_fail_count = 0U;
    kn_plan_probe_fail_count = 0U;
    kn_session_lost_posted = false;
    kn_last_terminal_outcome = MAPEK_PROBE_OUTCOME_SUCCESS;
    return;
  }
  if (outcome == MAPEK_PROBE_OUTCOME_NO_ANSWER ||
      outcome == MAPEK_PROBE_OUTCOME_TX_FAIL) {
    if (kn_probe_fail_count < UINT16_MAX) {
      kn_probe_fail_count++;
    }
    if (src == (uint8_t)MAPEK_PROBE_SOURCE_PLAN &&
        kn_plan_probe_fail_count < UINT16_MAX) {
      kn_plan_probe_fail_count++;
    }
    kn_last_terminal_outcome = outcome;
  }
}

static mapek_link_state_t analyze_link_state_locked(uint32_t now) {
  if (!mon_joined) {
    return MAPEK_LINK_STATE_OK;
  }

  const mapek_probe_outcome_t po = analyze_probe_outcome_locked(now);

  if (probe_armed && po == MAPEK_PROBE_OUTCOME_PENDING) {
    return MAPEK_LINK_STATE_PROBE_PENDING;
  }

  if (kn_probe_fail_count > 0U &&
      (po == MAPEK_PROBE_OUTCOME_NO_ANSWER ||
       po == MAPEK_PROBE_OUTCOME_TX_FAIL ||
       kn_last_terminal_outcome == MAPEK_PROBE_OUTCOME_NO_ANSWER ||
       kn_last_terminal_outcome == MAPEK_PROBE_OUTCOME_TX_FAIL)) {
    return MAPEK_LINK_STATE_DEGRADED;
  }

  if (last_lns_heard_ms == 0U ||
      (now - last_lns_heard_ms) >= MAPEK_LNS_HEARD_STALE_MS) {
    return MAPEK_LINK_STATE_STALE;
  }

  return MAPEK_LINK_STATE_OK;
}

static void snapshot_fill_locked(mapek_link_monitor_snapshot_t *out) {
  out->joined = mon_joined;
  out->join_transition_uptime_ms = mon_join_transition_ms;
  out->last_lns_heard_uptime_ms = last_lns_heard_ms;
  out->last_lns_heard_kind = last_lns_heard_kind;
  out->dl_rf_have = dl_rf_have;
  out->last_dl_rssi = dl_last_rssi;
  out->last_dl_snr = dl_last_snr;
  out->ewma_dl_rssi =
      (ewma_rssi_q8 != INT32_MIN) ? (int16_t)(ewma_rssi_q8 >> 8) : (int16_t)0;
  out->ewma_dl_snr =
      (ewma_snr_q8 != INT32_MIN) ? (int8_t)(ewma_snr_q8 >> 8) : (int8_t)0;
  out->lc_rf_have = lc_rf_have;
  out->last_lc_margin_db = lc_last_margin;
  out->last_lc_nb_gw = lc_last_gw;
  out->last_lc_ans_uptime_ms = lc_last_ans_ms;
  out->probe_armed = probe_armed;
  out->probe_sent_uptime_ms = probe_sent_ms;
  out->probe_baseline_heard_ms = probe_baseline_heard_ms;
  out->probe_tx_ok = probe_tx_ok;
  out->probe_rx_lc_ok = probe_rx_lc_ok;
  out->probe_rx_lns_ok = probe_rx_lns_ok;
  out->probe_source = probe_source;
  out->probe_arm_count = probe_arm_count;
}

static uint32_t mon_heard_age_ms_locked(uint32_t now) {
  if (last_lns_heard_ms == 0U) {
    return 0U;
  }
  return now - last_lns_heard_ms;
}

static void analyze_reset_locked(void) {
  memset(&an_last, 0, sizeof(an_last));
  an_last.rf_state = MAPEK_LINK_RF_UNKNOWN;
  an_last.link_state = MAPEK_LINK_STATE_OK;
  an_last.probe_outcome = MAPEK_PROBE_OUTCOME_NONE;
  log_prev_link_state = MAPEK_LINK_STATE_OK;
  analyze_prev_outcome = MAPEK_PROBE_OUTCOME_NONE;
}

static void analyze_run_locked(void) {
  const uint32_t now = k_uptime_get_32();
  uint32_t rsn = 0U;
  mapek_probe_outcome_t po;
  char ev[40];

  an_last.session_joined = mon_joined;
  an_last.lns_heard = (last_lns_heard_ms != 0U);
  an_last.lns_heard_age_ms = an_last.lns_heard ? mon_heard_age_ms_locked(now) : 0U;
  an_last.probe_age_ms =
      (probe_armed && probe_sent_ms != 0U) ? (now - probe_sent_ms) : 0U;
  an_last.probe_tx_ok = probe_tx_ok;
  an_last.probe_rx_lc_ok = probe_rx_lc_ok;
  an_last.probe_rx_lns_ok = probe_rx_lns_ok;
  an_last.probe_fail_count = kn_probe_fail_count;

  po = analyze_probe_outcome_locked(now);
  if (po != analyze_prev_outcome &&
      (po == MAPEK_PROBE_OUTCOME_SUCCESS || po == MAPEK_PROBE_OUTCOME_NO_ANSWER ||
       po == MAPEK_PROBE_OUTCOME_TX_FAIL)) {
    const uint8_t src = probe_source;

    knowledge_on_probe_terminal_locked(po);
    probe_disarm_locked();
    analyze_prev_outcome = po;
    an_last.probe_outcome = po;
    an_last.link_state = analyze_link_state_locked(now);
    (void)snprintf(ev, sizeof(ev), "probe %s src=%s",
                   mapek_probe_outcome_str(po), mapek_probe_src_str(src));
    mapek_log_locked(ev);
  } else if (po == MAPEK_PROBE_OUTCOME_NONE) {
    analyze_prev_outcome = MAPEK_PROBE_OUTCOME_NONE;
    an_last.probe_outcome = po;
    an_last.link_state = analyze_link_state_locked(now);
  } else {
    an_last.probe_outcome = po;
    an_last.link_state = analyze_link_state_locked(now);
  }

  if (!an_last.lns_heard) {
    rsn |= MAPEK_ANALYZE_REASON_NEVER_HEARD;
    an_last.rf_state = MAPEK_LINK_RF_UNKNOWN;
    an_last.ewma_dl_rssi = 0;
    an_last.ewma_dl_snr = 0;
  } else if (mon_dl_ewma_ready_locked()) {
    an_last.ewma_dl_rssi = (int16_t)(ewma_rssi_q8 >> 8);
    an_last.ewma_dl_snr = (int8_t)(ewma_snr_q8 >> 8);
    an_last.rf_state = rf_from_dl_ewma_locked();
    if (an_last.ewma_dl_rssi <= MAPEK_RF_RSSI_FAIR_DB) {
      rsn |= MAPEK_ANALYZE_REASON_WEAK_DL_RSSI;
    }
    if (an_last.ewma_dl_snr < MAPEK_RF_SNR_FAIR_MIN) {
      rsn |= MAPEK_ANALYZE_REASON_WEAK_DL_SNR;
    }
  } else {
    an_last.ewma_dl_rssi = 0;
    an_last.ewma_dl_snr = 0;
    an_last.rf_state = MAPEK_LINK_RF_UNKNOWN;
    rsn |= MAPEK_ANALYZE_REASON_NO_DL_RSSI;
  }

  if (an_last.link_state == MAPEK_LINK_STATE_STALE) {
    rsn |= MAPEK_ANALYZE_REASON_STALE_HEARD;
  }
  if (an_last.probe_outcome == MAPEK_PROBE_OUTCOME_PENDING) {
    rsn |= MAPEK_ANALYZE_REASON_PROBE_PENDING;
  } else if (an_last.probe_outcome == MAPEK_PROBE_OUTCOME_NO_ANSWER ||
             an_last.probe_outcome == MAPEK_PROBE_OUTCOME_TX_FAIL) {
    rsn |= MAPEK_ANALYZE_REASON_PROBE_FAIL;
  }

  an_last.reasons = rsn;

  if (an_last.link_state != log_prev_link_state) {
    (void)snprintf(ev, sizeof(ev), "st %s->%s", mapek_st_str(log_prev_link_state),
                   mapek_st_str(an_last.link_state));
    log_prev_link_state = an_last.link_state;
    mapek_log_locked(ev);
  }
}

static uint32_t plan_probe_cooldown_ms_locked(void) {
  if (kn_probe_fail_count >= MAPEK_PLAN_FAIL_COUNT_LONG_COOLDOWN) {
    return MAPEK_PLAN_PROBE_COOLDOWN_FAIL_MS;
  }
  return MAPEK_PLAN_PROBE_COOLDOWN_MS;
}

static void plan_execute_locked(void) {
  mapek_plan_intent_t intent = MAPEK_PLAN_INTENT_NONE;
  const uint32_t now = k_uptime_get_32();
  const uint32_t cooldown_ms = plan_probe_cooldown_ms_locked();
  char ev[32];

  if (!an_last.session_joined) {
    return;
  }

  if (an_last.probe_outcome == MAPEK_PROBE_OUTCOME_PENDING) {
    return;
  }

#if MAPEK_PLAN_LINKCHECK_PERIOD_MS > 0U
  if (plan_last_linkcheck_req_ms != 0U &&
      (now - plan_last_linkcheck_req_ms) >= MAPEK_PLAN_LINKCHECK_PERIOD_MS) {
    intent = MAPEK_PLAN_INTENT_PROBE_LC;
  }
#endif

  if (intent == MAPEK_PLAN_INTENT_NONE) {
    if (!an_last.lns_heard) {
      intent = MAPEK_PLAN_INTENT_PROBE_LC;
    } else if (an_last.link_state == MAPEK_LINK_STATE_STALE ||
               an_last.link_state == MAPEK_LINK_STATE_DEGRADED) {
      intent = MAPEK_PLAN_INTENT_PROBE_LC;
    }
  }

  if (intent == MAPEK_PLAN_INTENT_PROBE_LC && kn_last_plan_probe_ms != 0U &&
      (now - kn_last_plan_probe_ms) < cooldown_ms) {
    intent = MAPEK_PLAN_INTENT_NONE;
  }

  if (intent != MAPEK_PLAN_INTENT_NONE) {
    kn_last_plan_probe_ms = now;
    plan_last_linkcheck_req_ms = now;
    mapek_link_tag_next_probe_source((uint8_t)MAPEK_PROBE_SOURCE_PLAN);
    lora_request_link_check(true);
    (void)snprintf(ev, sizeof(ev), "plan LC cd=%us",
                   (unsigned)(cooldown_ms / 1000U));
    mapek_log_locked(ev);
  }
}

static void execute_session_lost_locked(void) {
#if MAPEK_SESSION_LOST_ENABLE
  if (!mon_joined || kn_session_lost_posted) {
    return;
  }
  if (an_last.probe_outcome == MAPEK_PROBE_OUTCOME_PENDING) {
    return;
  }
  if (an_last.link_state != MAPEK_LINK_STATE_STALE &&
      an_last.link_state != MAPEK_LINK_STATE_DEGRADED) {
    return;
  }
  if (kn_plan_probe_fail_count < MAPEK_SESSION_LOST_PROBE_FAILS) {
    return;
  }

  kn_session_lost_posted = true;
  mapek_log_wrn_locked("session_lost");
  lora_request_session_lost();
#endif
}

static void monitor_reset_locked(void) {
  ewma_rssi_q8 = INT32_MIN;
  ewma_snr_q8 = INT32_MIN;
  mon_joined = false;
  mon_join_transition_ms = 0U;
  last_lns_heard_ms = 0U;
  last_lns_heard_kind = (uint8_t)MAPEK_LNS_HEARD_NONE;
  dl_rf_have = false;
  lc_rf_have = false;
  probe_disarm_locked();
  probe_arm_count = 0U;
  kn_probe_fail_count = 0U;
  kn_plan_probe_fail_count = 0U;
  kn_last_plan_probe_ms = 0U;
  plan_last_linkcheck_req_ms = 0U;
  kn_session_lost_posted = false;
  kn_last_terminal_outcome = MAPEK_PROBE_OUTCOME_NONE;
  kn_next_probe_source = (uint8_t)MAPEK_PROBE_SOURCE_UNKNOWN;
  analyze_reset_locked();
}

void mapek_link_init(void) {
  k_mutex_lock(&mon_mutex, K_FOREVER);
  monitor_reset_locked();
  k_mutex_unlock(&mon_mutex);

  LOG_INF("mapek: ev init stale_s=%u ans_s=%u plan_cd_s=%u pf_max=%u",
          (unsigned)(MAPEK_LNS_HEARD_STALE_MS / 1000U),
          (unsigned)(MAPEK_PROBE_ANS_TIMEOUT_MS / 1000U),
          (unsigned)(MAPEK_PLAN_PROBE_COOLDOWN_MS / 1000U),
          (unsigned)MAPEK_SESSION_LOST_PROBE_FAILS);
}

void mapek_link_step(void) {
  k_mutex_lock(&mon_mutex, K_FOREVER);
  analyze_run_locked();
  plan_execute_locked();
  execute_session_lost_locked();
  k_mutex_unlock(&mon_mutex);
}

void mapek_link_feed_link_check(uint8_t demod_margin_db, uint8_t nb_gateways) {
  if (k_mutex_lock(&mon_mutex, K_NO_WAIT) != 0) {
    return;
  }

  lc_rf_have = true;
  lc_last_margin = demod_margin_db;
  lc_last_gw = nb_gateways;
  lc_last_ans_ms = k_uptime_get_32();

  mon_mark_lns_heard_locked((uint8_t)MAPEK_LNS_HEARD_LINK_CHECK);

  if (probe_armed && probe_sent_ms != 0U && lc_last_ans_ms >= probe_sent_ms) {
    probe_rx_lc_ok = true;
    probe_tx_ok = true;
    mon_probe_note_lns_rx_locked();
  }

  k_mutex_unlock(&mon_mutex);
}

void mapek_link_feed_dl(int16_t rssi, int8_t snr, uint8_t feed_flags) {
  ARG_UNUSED(feed_flags);

  if (k_mutex_lock(&mon_mutex, K_NO_WAIT) != 0) {
    return;
  }

  dl_rf_have = true;
  dl_last_rssi = rssi;
  dl_last_snr = snr;
  mon_mark_lns_heard_locked((uint8_t)MAPEK_LNS_HEARD_DL);
  ewma_i16_q8(&ewma_rssi_q8, rssi);
  ewma_i8_q8(&ewma_snr_q8, snr);
  mon_probe_note_lns_rx_locked();

  k_mutex_unlock(&mon_mutex);
}

void mapek_link_tag_next_probe_source(uint8_t source) {
  k_mutex_lock(&mon_mutex, K_FOREVER);
  kn_next_probe_source = source;
  k_mutex_unlock(&mon_mutex);
}

void mapek_link_feed_probe_begin(uint8_t source) {
  k_mutex_lock(&mon_mutex, K_FOREVER);

  if (kn_next_probe_source != (uint8_t)MAPEK_PROBE_SOURCE_UNKNOWN) {
    source = kn_next_probe_source;
    kn_next_probe_source = (uint8_t)MAPEK_PROBE_SOURCE_UNKNOWN;
  }

  /* New arm: allow the next terminal outcome to increment pf (Plan often
   * queues the following LC in the same step as NO_ANSWER, leaving
   * analyze_prev_outcome stuck on NO_ANSWER while the new probe is PENDING). */
  analyze_prev_outcome = MAPEK_PROBE_OUTCOME_NONE;

  probe_armed = true;
  probe_sent_ms = k_uptime_get_32();
  probe_baseline_heard_ms = last_lns_heard_ms;
  probe_tx_ok = false;
  probe_rx_lc_ok = false;
  probe_rx_lns_ok = false;
  probe_source = source;
  if (probe_arm_count < UINT32_MAX) {
    probe_arm_count++;
  }

  k_mutex_unlock(&mon_mutex);
}

void mapek_link_feed_probe_tx(int tx_ret) {
  k_mutex_lock(&mon_mutex, K_FOREVER);
  if (!probe_armed) {
    k_mutex_unlock(&mon_mutex);
    return;
  }
  probe_tx_ok = (tx_ret == 0);
  k_mutex_unlock(&mon_mutex);
}

void mapek_link_feed_probe_armed(int tx_ret, uint8_t source) {
  mapek_link_feed_probe_begin(source);
  mapek_link_feed_probe_tx(tx_ret);
}

void mapek_link_feed_join(bool joined) {
  char ev[8];

  k_mutex_lock(&mon_mutex, K_FOREVER);

  if (mon_joined != joined) {
    mon_joined = joined;
    mon_join_transition_ms = k_uptime_get_32();
    if (joined) {
      plan_last_linkcheck_req_ms = k_uptime_get_32();
      kn_session_lost_posted = false;
      kn_plan_probe_fail_count = 0U;
    }
  }

  if (!joined) {
    dl_rf_have = false;
    ewma_rssi_q8 = INT32_MIN;
    ewma_snr_q8 = INT32_MIN;
    last_lns_heard_ms = 0U;
    last_lns_heard_kind = (uint8_t)MAPEK_LNS_HEARD_NONE;
    lc_rf_have = false;
    probe_disarm_locked();
    kn_probe_fail_count = 0U;
    kn_plan_probe_fail_count = 0U;
    kn_last_plan_probe_ms = 0U;
    plan_last_linkcheck_req_ms = 0U;
    kn_session_lost_posted = false;
    kn_next_probe_source = (uint8_t)MAPEK_PROBE_SOURCE_UNKNOWN;
    kn_last_terminal_outcome = MAPEK_PROBE_OUTCOME_NONE;
    analyze_reset_locked();
  }

  analyze_run_locked();
  (void)snprintf(ev, sizeof(ev), "join=%u", joined ? 1U : 0U);
  mapek_log_locked(ev);

  k_mutex_unlock(&mon_mutex);
}

bool mapek_link_monitor_get(mapek_link_monitor_snapshot_t *out) {
  if (out == NULL) {
    return false;
  }
  k_mutex_lock(&mon_mutex, K_FOREVER);
  snapshot_fill_locked(out);
  k_mutex_unlock(&mon_mutex);
  return true;
}

bool mapek_link_analyze_get(mapek_link_analyze_snapshot_t *out) {
  if (out == NULL) {
    return false;
  }
  k_mutex_lock(&mon_mutex, K_FOREVER);
  analyze_run_locked();
  *out = an_last;
  k_mutex_unlock(&mon_mutex);
  return true;
}
