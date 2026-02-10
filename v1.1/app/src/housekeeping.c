/*
 * Housekeeping / heartbeat worker (FRD 4.9).
 *
 * Dedicated thread runs every HOUSEKEEPING_INTERVAL_SECONDS and posts
 * SMF_EVT_HOUSEKEEPING_TICK to the SMF queue. SMF owns rail arbitration and
 * runs housekeeping tasks (time sync, later link check, battery + uplink).
 * This thread does not call lora_* or rail_manager; it only posts events.
 */
#include "housekeeping.h"
#include "lora_app.h"
#include "smf_system_mode.h"
#include "sys_config.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(housekeeping, CONFIG_LOG_DEFAULT_LEVEL);

#define HOUSEKEEPING_STACK_SIZE 1024
#define HOUSEKEEPING_PRIORITY 9

static void housekeeping_run_tasks(void) {
  /* Post tick to SMF so it can run housekeeping with proper rail arbitration.
   * Only when joined so we don't flood SMF with no-op ticks. */
  if (lora_is_joined()) {
    LOG_DBG("[housekeeping] posting HOUSEKEEPING_TICK to SMF");
    (void)smf_post_event(SMF_EVT_HOUSEKEEPING_TICK, 0, k_uptime_get());
  }
  /* Future: SMF will also run link check, battery sample + heartbeat uplink. */
}

static void housekeeping_thread_fn(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  LOG_INF("[housekeeping] thread started, interval=%ds",
          HOUSEKEEPING_INTERVAL_SECONDS);

  for (;;) {
    k_sleep(K_SECONDS(HOUSEKEEPING_INTERVAL_SECONDS));
    housekeeping_run_tasks();
  }
}

K_THREAD_DEFINE(housekeeping_thread_id, HOUSEKEEPING_STACK_SIZE,
                housekeeping_thread_fn, NULL, NULL, NULL, HOUSEKEEPING_PRIORITY,
                0, -1);

int housekeeping_init(void) {
  /* Thread is already defined; main will
   * k_thread_start(housekeeping_thread_id). No per-run resources needed. */
  return 0;
}
