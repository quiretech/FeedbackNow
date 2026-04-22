/**
 * LoRa link stats: LinkCheckAns demod margin + gateway count.
 *
 * Source: Zephyr LoRaWAN stack calls the LinkCheck answer callback from its
 * MAC confirm context whenever a LinkCheckAns lands (see
 * lorawan_register_link_check_ans_callback in zephyr/lorawan/lorawan.h).
 *
 * Usage:
 *  - lora_link_stats_init() once, very early (before lorawan_start()).
 *  - lora_app_init() calls lora_link_stats_register() to hook the MAC callback.
 *  - Any thread can read the current snapshot via lora_link_stats_get().
 *
 * All state is atomic; the callback only updates atomics and never blocks.
 */
#ifndef LORA_LINK_STATS_H
#define LORA_LINK_STATS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinel for "no LinkCheckAns received yet". */
#define LORA_LINK_STATS_MARGIN_NONE ((int16_t)INT16_MIN)

typedef struct {
  /** Latest LinkCheckAns demod margin in dB (0..254). INT16_MIN if none. */
  int16_t last_demod_margin;
  /** Latest gateway count reported (0..255). 0 if none. */
  uint8_t last_nb_gateways;
  /** Best (highest) demod margin observed since boot. INT16_MIN if none. */
  int16_t best_demod_margin;
  /** Best (highest) gateway count observed since boot. */
  uint8_t best_nb_gateways;
  /** Number of LinkCheckAns responses received since boot (saturates at UINT16_MAX). */
  uint16_t samples;
  /** Uptime (ms) when the latest Ans landed. 0 if none. */
  uint32_t last_ans_uptime_ms;
} lora_link_stats_snapshot_t;

/**
 * Initialize the stats block to "no data". Safe to call multiple times.
 * Must run before lora_app_init() registers the MAC callback.
 */
void lora_link_stats_init(void);

/**
 * Register the MAC LinkCheckAns callback with the Zephyr LoRaWAN stack.
 * Call once from lora_app_init() after lorawan_start().
 */
void lora_link_stats_register(void);

/** Populate @a out with the current stats. Always safe; returns false if @a out is NULL. */
bool lora_link_stats_get(lora_link_stats_snapshot_t *out);

/** Convenience: true if we have at least one LinkCheckAns since boot. */
bool lora_link_stats_have_sample(void);

#ifdef __cplusplus
}
#endif

#endif /* LORA_LINK_STATS_H */
