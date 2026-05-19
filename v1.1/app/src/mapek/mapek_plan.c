#include "mapek/mapek_plan.h"

#include "mapek/mapek_config.h"

#include <stddef.h>

void mapek_plan_run(const analyze_out_t *ana, const mapek_knowledge_t *kb,
                    uint32_t now, plan_out_t *out) {
  if (ana == NULL || kb == NULL || out == NULL) {
    return;
  }

  *out = (plan_out_t){.intent = INTENT_NONE, .jitter_ms = 0U, .hb_due = false};

  if (!kb->joined) {
    return;
  }

  switch (ana->link_state) {
  case LINK_OK:
  case LINK_STALE:
  case LINK_DEGRADED:
    if (now >= kb->next_hb_due_ms) {
      out->intent = INTENT_FIRE_HEARTBEAT;
      out->hb_due = true;
    }
    break;
  default:
    break;
  }

  if (ana->hypothesis == HYPOTHESIS_SESSION_CORRUPT &&
      ana->link_state == LINK_SUSPECT_SESSION &&
      ana->urgency >= MAPEK_URGENCY_REJOIN_MIN) {
    if (kb->last_otaa_ms == 0U ||
        (now - kb->last_otaa_ms) >= MAPEK_OTAA_CYCLE_BACKOFF_MS) {
      out->intent = INTENT_REJOIN;
      out->jitter_ms = mapek_knowledge_jitter(MAPEK_OTAA_JITTER_MAX_MS);
    }
  }

  if (ana->hypothesis == HYPOTHESIS_DELETED &&
      ana->link_state == LINK_SUSPECT_DELETED &&
      ana->urgency >= MAPEK_URGENCY_REJOIN_SLOW_MIN) {
    if (kb->last_otaa_ms == 0U ||
        (now - kb->last_otaa_ms) >= MAPEK_DELETED_RETRY_MS) {
      out->intent = INTENT_REJOIN_SLOW;
      out->jitter_ms = mapek_knowledge_jitter(MAPEK_DELETED_JITTER_MAX_MS);
    }
  }

  if (out->intent == INTENT_FIRE_HEARTBEAT && ana->urgency < 10U &&
      ana->link_state == LINK_OK) {
    /* Normal daily HB — urgency gate only blocks rejoin, not health probe */
  }
}
