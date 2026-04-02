/*
 * Housekeeping / heartbeat worker (FRD 4.9).
 *
 * Dedicated thread runs at configured interval and posts
 * SMF_EVT_HOUSEKEEPING_TICK to the SMF queue. With
 * HEARTBEAT_USE_DEVEUI_JITTER=1, runs once per day at 00:00 UTC +
 * (DevEUI[7]*256+DevEUI[6]) % 1440 minutes to spread load (FRD 4.9). This
 * thread does not call lora_* or rail_manager; it only posts events.
 */
#include "housekeeping.h"
#include "eui_keys.h"
#include "lora_app.h"
#include "rtc.h"
#include "smf_system_mode.h"
#include "sys_config.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(housekeeping, CONFIG_LOG_DEFAULT_LEVEL);
K_SEM_DEFINE(housekeeping_ready_sem, 0, 1);

/* Use sys_config.h for HOUSEKEEPING_STACK_SIZE */
#define HOUSEKEEPING_PRIORITY 9

/** Compute seconds until next daily heartbeat time (00:00 UTC +
 * offset_minutes). */
static uint32_t seconds_until_next_heartbeat(uint32_t now_epoch,
                                             uint32_t offset_minutes) {
  uint32_t offset_sec = offset_minutes * 60U;
  uint32_t sec_since_midnight = now_epoch % SECONDS_PER_DAY;
  uint32_t next_run_epoch;

  if (offset_sec > sec_since_midnight) {
    next_run_epoch =
        (now_epoch / SECONDS_PER_DAY) * SECONDS_PER_DAY + offset_sec;
  } else {
    next_run_epoch =
        (now_epoch / SECONDS_PER_DAY + 1U) * SECONDS_PER_DAY + offset_sec;
  }
  return next_run_epoch - now_epoch;
}

static void housekeeping_run_tasks(void) {
  /* Post tick to SMF so it can run housekeeping with proper rail arbitration.
   * Only when joined so we don't flood SMF with no-op ticks. */
  if (lora_is_joined()) {
    LOG_DBG("[housekeeping] posting HOUSEKEEPING_TICK to SMF");
    (void)smf_post_event(SMF_EVT_HOUSEKEEPING_TICK, 0, k_uptime_get());
  }
}

static void housekeeping_thread_fn(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  static const uint8_t dev_eui[] = LORAWAN_DEV_EUI;
  uint32_t offset_minutes =
      (uint32_t)(dev_eui[7] * 256U + dev_eui[6]) % (uint32_t)MINUTES_PER_DAY;

  LOG_INF("[housekeeping] thread started, jitter=%d offset_min=%u",
          HEARTBEAT_USE_DEVEUI_JITTER, (unsigned)offset_minutes);
  k_sem_give(&housekeeping_ready_sem);

  for (;;) {
#if HEARTBEAT_USE_DEVEUI_JITTER
    uint32_t now_epoch = 0;
    int r = rtc_get_epoch_seconds(&now_epoch);
    if (r != 0 || now_epoch == 0) {
      /* RTC not set or unavailable: fallback to fixed interval */
      k_sleep(K_SECONDS(HOUSEKEEPING_INTERVAL_SECONDS));
      housekeeping_run_tasks();
      continue;
    }
    uint32_t sleep_sec =
        seconds_until_next_heartbeat(now_epoch, offset_minutes);
    /* Cap sleep to avoid overflow / unreasonable delay */
    if (sleep_sec > SECONDS_PER_DAY) {
      sleep_sec = HOUSEKEEPING_INTERVAL_SECONDS;
    }
    if (sleep_sec == 0) {
      sleep_sec = 1;
    }
    LOG_DBG("[housekeeping] sleeping %u s until next heartbeat",
            (unsigned)sleep_sec);
    k_sleep(K_SECONDS(sleep_sec));
#else
    k_sleep(K_SECONDS(HOUSEKEEPING_INTERVAL_SECONDS));
#endif
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

int housekeeping_wait_until_ready(k_timeout_t timeout) {
  return k_sem_take(&housekeeping_ready_sem, timeout);
}
