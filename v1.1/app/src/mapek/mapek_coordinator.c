#include "mapek_coordinator.h"

#include "mapek/mapek_analyze.h"
#include "mapek/mapek_config.h"
#include "mapek/mapek_execute.h"
#include "mapek/mapek_knowledge.h"
#include "mapek/mapek_mon.h"
#include "mapek/mapek_plan.h"

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(mapek, CONFIG_LOG_DEFAULT_LEVEL);

#define MAPEK_STEP_MS 5000U
#define MAPEK_STACK_SIZE 1536U
#define MAPEK_PRIORITY 8

static atomic_t uplink_allowed_flag = ATOMIC_INIT(1);
static link_state_t log_link_state = LINK_OK;
static failure_hypothesis_t log_hypothesis = HYPOTHESIS_UNKNOWN;
static heartbeat_result_t log_hb = HB_NOT_FIRED;
static bool prev_joined;

static const char *st_str(link_state_t st) {
  switch (st) {
  case LINK_STALE:
    return "STALE";
  case LINK_DEGRADED:
    return "DEGRADED";
  case LINK_SUSPECT_SESSION:
    return "SUSPECT";
  case LINK_SUSPECT_DELETED:
    return "DELETED";
  default:
    return "OK";
  }
}

static const char *hyp_str(failure_hypothesis_t h) {
  switch (h) {
  case HYPOTHESIS_RF_ISSUE:
    return "RF";
  case HYPOTHESIS_BACKHAUL_OUTAGE:
    return "F2";
  case HYPOTHESIS_SESSION_CORRUPT:
    return "F3";
  case HYPOTHESIS_DELETED:
    return "F4";
  default:
    return "?";
  }
}

static const char *hb_str(heartbeat_result_t hb) {
  switch (hb) {
  case HB_PENDING:
    return "PENDING";
  case HB_SUCCESS:
    return "OK";
  case HB_NO_ANSWER:
    return "NO_ANSWER";
  case HB_TX_FAIL:
    return "TX_FAIL";
  default:
    return "idle";
  }
}

static void schedule_next_hb(mapek_knowledge_t *kb, uint32_t now) {
  const uint32_t base = kb->hb_use_short_cadence ? MAPEK_STALE_HB_CADENCE_MS
                                                 : MAPEK_DAILY_HB_INTERVAL_MS;
  const uint32_t jit_max = kb->hb_use_short_cadence ? MAPEK_STALE_HB_JITTER_MS
                                                    : MAPEK_DAILY_HB_JITTER_MS;
  kb->next_hb_due_ms = now + base + mapek_knowledge_jitter(jit_max);
}

static void mapek_log_ev(const char *ev, const analyze_out_t *ana,
                         const mapek_knowledge_t *kb, const mon_snap_t *mon) {
  if (mon->lns_ever_heard) {
    LOG_INF("mapek: ev=%s j=%u st=%s hyp=%s conf=%u urg=%u heard=%us "
            "hb=%s f2=%u f3=%u otaa=%u",
            ev, (unsigned)kb->joined, st_str(ana->link_state),
            hyp_str(ana->hypothesis), (unsigned)ana->confidence,
            (unsigned)ana->urgency,
            (unsigned)(mon->lns_heard_age_ms / 1000U), hb_str(mon->hb_result),
            (unsigned)ana->f2_score, (unsigned)ana->f3_score,
            (unsigned)kb->otaa_cycle_count);
  } else {
    LOG_INF("mapek: ev=%s j=%u st=%s hyp=%s conf=%u urg=%u heard=never "
            "hb=%s f2=%u f3=%u otaa=%u",
            ev, (unsigned)kb->joined, st_str(ana->link_state),
            hyp_str(ana->hypothesis), (unsigned)ana->confidence,
            (unsigned)ana->urgency, hb_str(mon->hb_result),
            (unsigned)ana->f2_score, (unsigned)ana->f3_score,
            (unsigned)kb->otaa_cycle_count);
  }
}

static void kb_on_hb_terminal(mapek_knowledge_t *kb, heartbeat_result_t hb,
                              uint32_t now) {
  if (hb == HB_SUCCESS) {
    kb->hb_fail_streak = 0U;
    kb->hb_ok_streak++;
    kb->hb_use_short_cadence = false;
    kb->degraded_since_ms = 0U;
    kb->otaa_cycle_count = 0U;
    kb->session_lost_posted = false;
    schedule_next_hb(kb, now);
  } else if (hb == HB_NO_ANSWER || hb == HB_TX_FAIL) {
    if (kb->hb_fail_streak < UINT16_MAX) {
      kb->hb_fail_streak++;
    }
    kb->hb_ok_streak = 0U;
    kb->hb_use_short_cadence = true;
    schedule_next_hb(kb, now);
  }
  kb->hb_result = hb;
}

static void kb_commit_state(mapek_knowledge_t *kb, const analyze_out_t *ana,
                            const mon_snap_t *mon, uint32_t now) {
  if (kb->committed_link_state == ana->link_state) {
    return;
  }
  char ev[32];
  (void)snprintf(ev, sizeof(ev), "st %s->%s",
                 st_str(kb->committed_link_state), st_str(ana->link_state));
  kb->state_since_ms = now;
  kb->committed_link_state = ana->link_state;
  if (ana->link_state == LINK_DEGRADED && kb->degraded_since_ms == 0U) {
    kb->degraded_since_ms = now;
  }
  if (ana->link_state == LINK_OK) {
    kb->degraded_since_ms = 0U;
    kb->otaa_cycle_count = 0U;
    kb->session_lost_posted = false;
    kb->hb_use_short_cadence = false;
  }
  mapek_log_ev(ev, ana, kb, mon);
}

static void mapek_coordinator_fn(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  mon_snap_t mon = {0};
  analyze_out_t ana = {0};
  plan_out_t plan = {0};
  mapek_knowledge_t kb = {0};

  for (;;) {
    k_msleep(MAPEK_STEP_MS);

    const uint32_t now = k_uptime_get_32();

    mapek_mon_snapshot(&mon);
    mon.now_ms = now;
    mapek_knowledge_get(&kb);
    kb.joined = mon.joined;
    mapek_knowledge_update(&kb);

    if (mon.joined && !prev_joined) {
      kb.hb_fail_streak = 0U;
      kb.committed_link_state = LINK_OK;
      kb.state_since_ms = now;
      schedule_next_hb(&kb, now);
      mapek_knowledge_update(&kb);
    }
    prev_joined = mon.joined;

    const heartbeat_result_t prev_hb = kb.hb_result;

    if (kb.hb_result == HB_PENDING && kb.last_hb_fired_ms != 0U &&
        (now - kb.last_hb_fired_ms) >= MAPEK_HB_ANS_TIMEOUT_MS) {
      mapek_feed_heartbeat_result(HB_NO_ANSWER);
      mapek_mon_snapshot(&mon);
      mon.now_ms = now;
    }

    if (mon.hb_result != prev_hb &&
        (mon.hb_result == HB_SUCCESS || mon.hb_result == HB_NO_ANSWER ||
         mon.hb_result == HB_TX_FAIL)) {
      kb_on_hb_terminal(&kb, mon.hb_result, now);
      mapek_knowledge_update(&kb);
      analyze_out_t ana_hb = {0};
      ana_hb.link_state = kb.committed_link_state;
      ana_hb.hypothesis = kb.last_hypothesis;
      mapek_log_ev("hb", &ana_hb, &kb, &mon);
      log_hb = mon.hb_result;
    }

    mapek_analyze_run(&mon, &kb, &ana);
    kb.last_hypothesis = ana.hypothesis;

    if (ana.link_state != log_link_state) {
      log_link_state = ana.link_state;
    }
    if (ana.hypothesis != log_hypothesis) {
      log_hypothesis = ana.hypothesis;
      mapek_log_ev("hyp", &ana, &kb, &mon);
    }

    kb_commit_state(&kb, &ana, &mon, now);
    kb.current_link_state = ana.link_state;
    mapek_knowledge_update(&kb);

    mapek_knowledge_get(&kb);
    kb.joined = mon.joined;
    mapek_plan_run(&ana, &kb, now, &plan);

    if (plan.intent == INTENT_REJOIN || plan.intent == INTENT_REJOIN_SLOW) {
      mapek_log_ev("otaa", &ana, &kb, &mon);
    }

    mapek_execute_run(&plan, &ana, &kb, now);

    if (plan.intent == INTENT_FIRE_HEARTBEAT) {
      mapek_feed_heartbeat_tx(0);
    }

    atomic_set(&uplink_allowed_flag, ana.uplink_allowed ? 1 : 0);
  }
}

K_THREAD_DEFINE(mapek_coord_tid, MAPEK_STACK_SIZE, mapek_coordinator_fn, NULL,
                NULL, NULL, MAPEK_PRIORITY, 0, -1);

void mapek_init(const uint8_t dev_eui[8]) {
  mapek_mon_init();
  mapek_knowledge_init(dev_eui);
  prev_joined = false;
  log_link_state = LINK_OK;
  log_hypothesis = HYPOTHESIS_UNKNOWN;
  log_hb = HB_NOT_FIRED;

  mapek_knowledge_t kb = {0};
  mapek_knowledge_get(&kb);
  kb.next_hb_due_ms =
      MAPEK_DAILY_HB_INTERVAL_MS + mapek_knowledge_jitter(MAPEK_DAILY_HB_JITTER_MS);
  mapek_knowledge_update(&kb);

  LOG_INF("mapek: init stale=%us hb_ans=%us backhaul=%us",
          (unsigned)(MAPEK_STALE_THRESHOLD_MS / 1000U),
          (unsigned)(MAPEK_HB_ANS_TIMEOUT_MS / 1000U),
          (unsigned)(MAPEK_BACKHAUL_WAIT_MS / 1000U));
}

void mapek_start(void) { k_thread_start(mapek_coord_tid); }

bool mapek_uplink_allowed(void) {
  return atomic_get(&uplink_allowed_flag) != 0;
}

link_state_t mapek_link_state_get(void) {
  mapek_knowledge_t kb = {0};
  mapek_knowledge_get(&kb);
  return kb.current_link_state;
}
