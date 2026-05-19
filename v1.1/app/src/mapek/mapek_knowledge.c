#include "mapek/mapek_knowledge.h"

#include <stddef.h>
#include <zephyr/kernel.h>

static K_MUTEX_DEFINE(kb_mutex);
static mapek_knowledge_t kb;

static uint32_t jitter_fold(const uint8_t dev_eui[8]) {
  uint32_t s = 0U;
  for (int i = 0; i < 8; i++) {
    s ^= ((uint32_t)dev_eui[i] << ((unsigned)i & 3U));
    s = (s << 5) | (s >> 27);
  }
  return s ? s : 1U;
}

void mapek_knowledge_init(const uint8_t dev_eui[8]) {
  k_mutex_lock(&kb_mutex, K_FOREVER);
  kb = (mapek_knowledge_t){0};
  kb.jitter_seed = jitter_fold(dev_eui);
  kb.committed_link_state = LINK_OK;
  kb.current_link_state = LINK_OK;
  kb.hb_result = HB_NOT_FIRED;
  k_mutex_unlock(&kb_mutex);
}

void mapek_knowledge_get(mapek_knowledge_t *out) {
  if (out == NULL) {
    return;
  }
  k_mutex_lock(&kb_mutex, K_FOREVER);
  *out = kb;
  k_mutex_unlock(&kb_mutex);
}

void mapek_knowledge_update(const mapek_knowledge_t *in) {
  if (in == NULL) {
    return;
  }
  k_mutex_lock(&kb_mutex, K_FOREVER);
  kb = *in;
  k_mutex_unlock(&kb_mutex);
}

uint32_t mapek_knowledge_jitter(uint32_t max_ms) {
  if (max_ms == 0U) {
    return 0U;
  }
  k_mutex_lock(&kb_mutex, K_FOREVER);
  kb.jitter_seed = kb.jitter_seed * 1103515245U + 12345U;
  const uint32_t v = (kb.jitter_seed >> 16) % (max_ms + 1U);
  k_mutex_unlock(&kb_mutex);
  return v;
}
