#include "mapek/mapek_analyze.h"

#include "mapek/mapek_config.h"

#include <stddef.h>

static rf_quality_t rf_from_ewma(const mon_snap_t *mon) {
  if (!mon->dl_rf_valid) {
    return RF_UNKNOWN;
  }
  const int16_t er = mon->ewma_rssi;
  const int8_t es = mon->ewma_snr;

  if (er <= MAPEK_RF_RSSI_POOR_DB || es < MAPEK_RF_SNR_POOR_MIN) {
    return RF_POOR;
  }
  if (er <= MAPEK_RF_RSSI_FAIR_DB || es < MAPEK_RF_SNR_FAIR_MIN) {
    return RF_FAIR;
  }
  if (er <= MAPEK_RF_RSSI_GOOD_DB || es < MAPEK_RF_SNR_GOOD_MIN) {
    return RF_GOOD;
  }
  return RF_EXCELLENT;
}

static uint16_t score_backhaul(const mon_snap_t *mon, uint32_t degraded_ms) {
  uint32_t time_score = 0U;
  if (degraded_ms < MAPEK_BACKHAUL_WAIT_MS) {
    time_score =
        700U - (uint32_t)((uint64_t)degraded_ms * 700U / MAPEK_BACKHAUL_WAIT_MS);
  } else {
    const uint32_t over = degraded_ms - MAPEK_BACKHAUL_WAIT_MS;
    time_score = (over < MAPEK_BACKHAUL_WAIT_MS)
                     ? (400U - over * 400U / MAPEK_BACKHAUL_WAIT_MS)
                     : 0U;
  }

  uint16_t rf_boost = 0U;
  const rf_quality_t rf = rf_from_ewma(mon);
  if (rf == RF_EXCELLENT || rf == RF_GOOD) {
    rf_boost = 250U;
  } else if (rf == RF_FAIR) {
    rf_boost = 100U;
  }

  uint16_t hb_boost = 0U;
  if (mon->hb_result == HB_NO_ANSWER && rf != RF_POOR) {
    hb_boost = 200U;
  } else if (mon->hb_result == HB_PENDING && rf != RF_POOR) {
    hb_boost = 80U;
  }

  uint32_t s = time_score + rf_boost + hb_boost;
  return (uint16_t)(s > 1000U ? 1000U : s);
}

static uint16_t score_session(const mon_snap_t *mon, const mapek_knowledge_t *kb,
                              uint32_t degraded_ms) {
  (void)mon;
  uint32_t time_score = 0U;
  if (degraded_ms > MAPEK_BACKHAUL_WAIT_MS) {
    const uint32_t over = degraded_ms - MAPEK_BACKHAUL_WAIT_MS;
    time_score = (over < MAPEK_BACKHAUL_WAIT_MS)
                     ? (300U + over * 500U / MAPEK_BACKHAUL_WAIT_MS)
                     : 800U;
  }

  uint32_t s = time_score + (uint32_t)kb->otaa_cycle_count * 150U;
  if (kb->otaa_cycle_count >= MAPEK_OTAA_MAX_CYCLES) {
    s += 400U;
  }
  return (uint16_t)(s > 1000U ? 1000U : s);
}

static failure_hypothesis_t classify_scores(const mon_snap_t *mon,
                                            const mapek_knowledge_t *kb,
                                            uint32_t degraded_ms,
                                            uint16_t *f2, uint16_t *f3) {
  if (!mon->lns_ever_heard) {
    *f2 = 0U;
    *f3 = 0U;
    return HYPOTHESIS_UNKNOWN;
  }

  const rf_quality_t rf = rf_from_ewma(mon);
  if (mon->dl_rf_valid && rf == RF_POOR) {
    *f2 = 50U;
    *f3 = 30U;
    return HYPOTHESIS_RF_ISSUE;
  }

  *f2 = score_backhaul(mon, degraded_ms);
  *f3 = score_session(mon, kb, degraded_ms);

  if (kb->otaa_cycle_count >= MAPEK_OTAA_MAX_CYCLES) {
    return HYPOTHESIS_DELETED;
  }
  if (*f3 > *f2) {
    return HYPOTHESIS_SESSION_CORRUPT;
  }
  if (*f2 > 0U) {
    return HYPOTHESIS_BACKHAUL_OUTAGE;
  }
  return HYPOTHESIS_UNKNOWN;
}

static uint8_t hypothesis_confidence(uint16_t f2, uint16_t f3) {
  const uint16_t hi = (f2 > f3) ? f2 : f3;
  const uint16_t lo = (f2 > f3) ? f3 : f2;
  if (hi == 0U) {
    return 0U;
  }
  return (uint8_t)(((uint32_t)hi - lo) * 100U / hi);
}

static uint8_t compute_urgency(failure_hypothesis_t hyp, uint8_t confidence,
                               link_state_t raw_st, uint32_t degraded_ms,
                               uint16_t hb_fail_streak) {
  uint32_t u = 0U;

  if (raw_st == LINK_DEGRADED || raw_st == LINK_SUSPECT_SESSION ||
      raw_st == LINK_SUSPECT_DELETED) {
    u += 15U;
  }
  if (hb_fail_streak >= MAPEK_HB_FAIL_STREAK_DEGRADED) {
    u += 20U;
  }
  if (degraded_ms > MAPEK_BACKHAUL_WAIT_MS) {
    u += 25U;
  }
  if (degraded_ms > MAPEK_BACKHAUL_WAIT_MS + MAPEK_DEGRADED_TO_SUSPECT_MS) {
    u += 15U;
  }

  switch (hyp) {
  case HYPOTHESIS_RF_ISSUE:
    return (uint8_t)((u > 25U) ? 25U : u);
  case HYPOTHESIS_BACKHAUL_OUTAGE:
    u += 10U;
    break;
  case HYPOTHESIS_SESSION_CORRUPT:
    u += (uint32_t)confidence / 2U;
    break;
  case HYPOTHESIS_DELETED:
    u += 30U;
    break;
  default:
    break;
  }

  return (uint8_t)(u > 100U ? 100U : u);
}

static link_state_t raw_link_state(const mon_snap_t *mon,
                                   uint16_t hb_fail_streak) {
  if (!mon->joined) {
    return LINK_OK;
  }

  if (mon->hb_result == HB_SUCCESS) {
    return LINK_OK;
  }

  if ((mon->hb_result == HB_NO_ANSWER || mon->hb_result == HB_TX_FAIL) &&
      hb_fail_streak >= MAPEK_HB_FAIL_STREAK_DEGRADED) {
    return LINK_DEGRADED;
  }

  if (!mon->lns_ever_heard ||
      mon->lns_heard_age_ms >= MAPEK_STALE_THRESHOLD_MS) {
    return LINK_STALE;
  }

  return LINK_OK;
}

static link_state_t apply_hysteresis(const mapek_knowledge_t *kb,
                                     link_state_t raw, failure_hypothesis_t hyp,
                                     uint32_t degraded_ms, uint8_t urgency,
                                     uint32_t now) {
  const link_state_t committed = kb->committed_link_state;

  if (raw == LINK_OK) {
    return LINK_OK;
  }

  if (committed == LINK_OK) {
    return raw;
  }

  if (committed == LINK_STALE) {
    if (raw == LINK_DEGRADED) {
      return LINK_DEGRADED;
    }
    return LINK_STALE;
  }

  if (committed == LINK_DEGRADED) {
    if (raw == LINK_OK) {
      return LINK_OK;
    }
    const uint32_t in_degraded =
        (kb->degraded_since_ms != 0U && now >= kb->degraded_since_ms)
            ? (now - kb->degraded_since_ms)
            : 0U;
    const bool suspect_time =
        (in_degraded >= MAPEK_BACKHAUL_WAIT_MS + MAPEK_DEGRADED_TO_SUSPECT_MS);
    if (suspect_time &&
        (hyp == HYPOTHESIS_SESSION_CORRUPT || hyp == HYPOTHESIS_DELETED) &&
        urgency >= MAPEK_URGENCY_REJOIN_MIN) {
      return (hyp == HYPOTHESIS_DELETED) ? LINK_SUSPECT_DELETED
                                         : LINK_SUSPECT_SESSION;
    }
    return LINK_DEGRADED;
  }

  if (committed == LINK_SUSPECT_SESSION) {
    if (raw == LINK_OK) {
      return LINK_OK;
    }
    if (hyp == HYPOTHESIS_DELETED &&
        kb->otaa_cycle_count >= MAPEK_OTAA_MAX_CYCLES) {
      return LINK_SUSPECT_DELETED;
    }
    return LINK_SUSPECT_SESSION;
  }

  if (committed == LINK_SUSPECT_DELETED) {
    return (raw == LINK_OK) ? LINK_OK : LINK_SUSPECT_DELETED;
  }

  return raw;
}

void mapek_analyze_run(const mon_snap_t *mon, const mapek_knowledge_t *kb,
                       analyze_out_t *out) {
  if (mon == NULL || kb == NULL || out == NULL) {
    return;
  }

  uint32_t degraded_ms = 0U;
  if (kb->degraded_since_ms != 0U && mon->now_ms >= kb->degraded_since_ms) {
    degraded_ms = mon->now_ms - kb->degraded_since_ms;
  }

  out->rf_quality = rf_from_ewma(mon);
  out->degraded_duration_ms = degraded_ms;
  out->hypothesis =
      classify_scores(mon, kb, degraded_ms, &out->f2_score, &out->f3_score);
  out->confidence = hypothesis_confidence(out->f2_score, out->f3_score);

  const link_state_t raw = raw_link_state(mon, kb->hb_fail_streak);
  out->urgency =
      compute_urgency(out->hypothesis, out->confidence, raw, degraded_ms,
                      kb->hb_fail_streak);
  out->link_state = apply_hysteresis(kb, raw, out->hypothesis, degraded_ms,
                                     out->urgency, mon->now_ms);

  out->uplink_allowed =
      (out->link_state == LINK_OK || out->link_state == LINK_STALE);

  uint32_t rsn = 0U;
  if (!mon->lns_ever_heard) {
    rsn |= MAPEK_REASON_NEVER_HEARD;
  }
  if (out->link_state == LINK_STALE) {
    rsn |= MAPEK_REASON_STALE;
  }
  if (mon->hb_result == HB_NO_ANSWER) {
    rsn |= MAPEK_REASON_HB_NO_ANSWER;
  }
  if (mon->hb_result == HB_TX_FAIL) {
    rsn |= MAPEK_REASON_HB_TX_FAIL;
  }
  if (mon->dl_rf_valid && mon->ewma_rssi <= MAPEK_RF_RSSI_FAIR_DB) {
    rsn |= MAPEK_REASON_WEAK_RSSI;
  }
  if (mon->dl_rf_valid && mon->ewma_snr < MAPEK_RF_SNR_FAIR_MIN) {
    rsn |= MAPEK_REASON_WEAK_SNR;
  }
  if (degraded_ms > 0U && degraded_ms < MAPEK_BACKHAUL_WAIT_MS) {
    rsn |= MAPEK_REASON_BACKHAUL_WAIT;
  }
  if (out->link_state == LINK_SUSPECT_SESSION) {
    rsn |= MAPEK_REASON_SESSION_SUSPECT;
  }
  if (out->link_state == LINK_SUSPECT_DELETED) {
    rsn |= MAPEK_REASON_DELETED_SUSPECT;
  }
  out->reasons = rsn;
}
