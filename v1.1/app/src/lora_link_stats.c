/**
 * LoRa link stats: capture LinkCheckAns (demod_margin, nb_gateways) from the
 * Zephyr LoRaWAN MAC callback into an atomic snapshot readable by any thread.
 *
 * The callback runs from the LoRaWAN stack's MLME confirm context (see
 * mlme_confirm_handler in zephyr/subsys/lorawan/lorawan.c), so it MUST NOT
 * block, allocate, or call back into lorawan_*. We only touch atomics here.
 *
 * Only the callback writes fields; readers copy under a single mutex so a
 * snapshot is self-consistent. Callback vs reader contention is rare (one Ans
 * per few seconds at most); mutex is K_NO_WAIT from the callback to avoid any
 * risk of blocking the MAC thread.
 */
#include "lora_link_stats.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>

LOG_MODULE_REGISTER(lora_link_stats, CONFIG_LOG_DEFAULT_LEVEL);

static K_MUTEX_DEFINE(stats_mutex);

static lora_link_stats_snapshot_t stats;

static void stats_reset_locked(void) {
  stats.last_demod_margin = LORA_LINK_STATS_MARGIN_NONE;
  stats.last_nb_gateways = 0;
  stats.best_demod_margin = LORA_LINK_STATS_MARGIN_NONE;
  stats.best_nb_gateways = 0;
  stats.samples = 0;
  stats.last_ans_uptime_ms = 0;
}

/**
 * MAC callback. Runs in MAC confirm context (not an ISR, but not an app
 * thread either). Keep it short and non-blocking.
 */
static void on_link_check_ans(uint8_t demod_margin, uint8_t nb_gateways) {
  /* Try-lock only: never block the MAC thread. If the reader holds the lock,
   * we drop this sample; next Ans will catch up. */
  if (k_mutex_lock(&stats_mutex, K_NO_WAIT) != 0) {
    LOG_DBG("[LINK] Ans dropped (stats locked by reader)");
    return;
  }

  stats.last_demod_margin = (int16_t)demod_margin;
  stats.last_nb_gateways = nb_gateways;
  if (stats.samples < UINT16_MAX) {
    stats.samples++;
  }
  stats.last_ans_uptime_ms = (uint32_t)k_uptime_get();

  if (stats.best_demod_margin == LORA_LINK_STATS_MARGIN_NONE ||
      (int16_t)demod_margin > stats.best_demod_margin) {
    stats.best_demod_margin = (int16_t)demod_margin;
  }
  if (nb_gateways > stats.best_nb_gateways) {
    stats.best_nb_gateways = nb_gateways;
  }

  k_mutex_unlock(&stats_mutex);

  LOG_INF("[LINK] Ans margin=%u dB gateways=%u (samples=%u)",
          (unsigned)demod_margin, (unsigned)nb_gateways,
          (unsigned)stats.samples);
}

void lora_link_stats_init(void) {
  k_mutex_lock(&stats_mutex, K_FOREVER);
  stats_reset_locked();
  k_mutex_unlock(&stats_mutex);
}

void lora_link_stats_register(void) {
  lorawan_register_link_check_ans_callback(on_link_check_ans);
  LOG_INF("[LINK] LinkCheckAns callback registered");
}

bool lora_link_stats_get(lora_link_stats_snapshot_t *out) {
  if (out == NULL) {
    return false;
  }
  k_mutex_lock(&stats_mutex, K_FOREVER);
  *out = stats;
  k_mutex_unlock(&stats_mutex);
  return true;
}

bool lora_link_stats_have_sample(void) {
  k_mutex_lock(&stats_mutex, K_FOREVER);
  bool have = (stats.samples > 0);
  k_mutex_unlock(&stats_mutex);
  return have;
}
