#ifndef MAPEK_KNOWLEDGE_H
#define MAPEK_KNOWLEDGE_H

#include "mapek/mapek_types.h"

#include <stdint.h>

typedef struct {
  bool joined;

  uint32_t last_hb_fired_ms;
  uint32_t next_hb_due_ms;
  bool hb_use_short_cadence;

  uint32_t degraded_since_ms;
  uint16_t otaa_cycle_count;
  uint32_t last_otaa_ms;
  bool session_lost_posted;

  failure_hypothesis_t last_hypothesis;
  link_state_t committed_link_state;
  uint32_t state_since_ms;

  uint16_t hb_fail_streak;
  uint16_t hb_ok_streak;

  heartbeat_result_t hb_result;

  uint32_t jitter_seed;

  link_state_t current_link_state;
} mapek_knowledge_t;

void mapek_knowledge_init(const uint8_t dev_eui[8]);
void mapek_knowledge_get(mapek_knowledge_t *out);
void mapek_knowledge_update(const mapek_knowledge_t *in);
uint32_t mapek_knowledge_jitter(uint32_t max_ms);

#endif /* MAPEK_KNOWLEDGE_H */
