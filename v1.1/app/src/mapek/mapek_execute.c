#include "mapek/mapek_execute.h"

#include "lora_app.h"
#include "mapek/mapek_config.h"

#include <stddef.h>
#include <zephyr/kernel.h>

void mapek_execute_run(const plan_out_t *plan, const analyze_out_t *ana,
                       mapek_knowledge_t *kb, uint32_t now) {
  if (plan == NULL || ana == NULL || kb == NULL) {
    return;
  }

  switch (plan->intent) {
  case INTENT_FIRE_HEARTBEAT:
    kb->last_hb_fired_ms = now;
    kb->hb_result = HB_PENDING;
    mapek_knowledge_update(kb);
    lora_request_time_sync();
    lora_request_link_check(false);
    break;

  case INTENT_REJOIN:
    if (!kb->session_lost_posted) {
      kb->session_lost_posted = true;
      kb->last_otaa_ms = now;
      if (kb->otaa_cycle_count < UINT16_MAX) {
        kb->otaa_cycle_count++;
      }
      mapek_knowledge_update(kb);
      if (plan->jitter_ms > 0U) {
        k_msleep(plan->jitter_ms);
      }
      lora_request_session_lost();
    }
    break;

  case INTENT_REJOIN_SLOW:
    if (kb->last_otaa_ms == 0U ||
        (now - kb->last_otaa_ms) >= MAPEK_DELETED_RETRY_MS) {
      kb->last_otaa_ms = now;
      if (kb->otaa_cycle_count < UINT16_MAX) {
        kb->otaa_cycle_count++;
      }
      kb->session_lost_posted = true;
      mapek_knowledge_update(kb);
      if (plan->jitter_ms > 0U) {
        k_msleep(plan->jitter_ms);
      }
      lora_request_session_lost();
    }
    break;

  case INTENT_NONE:
  default:
    break;
  }

  kb->current_link_state = ana->link_state;
  mapek_knowledge_update(kb);
}
