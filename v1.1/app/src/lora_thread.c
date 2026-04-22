#include "devnonce_store.h"
#include "display_manager.h"
#include "join_state_store.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
#include "lora_link_stats.h"
#include "rail_manager.h"
#include "smf_system_mode.h"
#include "sys_config.h"
#include "time_sync.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(lora_thread, CONFIG_LOG_DEFAULT_LEVEL);

#define LORA_JOIN_RETRY_DELAY K_SECONDS(LORA_JOIN_RETRY_DELAY_SECONDS)

/* SF7/125 for time sync phase (DR0 downlinks often fail). Region-specific. */
#if defined(CONFIG_LORAMAC_REGION_EU868)
#define LORA_TIME_SYNC_DR LORAWAN_DR_5
#define LORA_TIME_SYNC_DR_STR "5 [EU868]"
#elif defined(CONFIG_LORAMAC_REGION_US915)
#define LORA_TIME_SYNC_DR LORAWAN_DR_3
#define LORA_TIME_SYNC_DR_STR "3 [US915]"
#else
#define LORA_TIME_SYNC_DR LORAWAN_DR_5
#define LORA_TIME_SYNC_DR_STR "5"
#endif

static uint8_t dev_eui[] = LORAWAN_DEV_EUI;
static uint8_t join_eui[] = LORAWAN_JOIN_EUI;
static uint8_t app_key[] = LORAWAN_APP_KEY;

/* Last uplink completion time (ms) for rate limiting; 0 = never sent yet */
static uint32_t last_uplink_ms;

/* Consecutive send failures; when >= LORA_SEND_FAILURES_BEFORE_BACKOFF we
 * clear joined and schedule re-join after backoff. */
static uint32_t consecutive_send_failures;

/* Next LORA_CMD_JOIN_SILENT may show join LED only when this is non-zero (set
 * for the auto-post right after boot when has_joined_once; cleared before
 * backoff rejoin and when handling LORA_CMD_JOIN). */
static atomic_t silent_join_installer_led_armed;

K_SEM_DEFINE(lora_ready_sem, 0, 1);

/** Join LED: deliberate join always; silent only for first cycle after boot
 *  auto-post (installer at power-on), never for backoff rejoin. */
static bool join_installer_led_for_cmd(uint8_t cmd) {
  if (cmd == LORA_CMD_JOIN) {
    (void)atomic_set(&silent_join_installer_led_armed, 0);
    return true;
  }
  if (cmd == LORA_CMD_JOIN_SILENT) {
    return atomic_set(&silent_join_installer_led_armed, 0) != 0;
  }
  return false;
}

static void join_after_backoff_timer_expiry(struct k_timer *timer);
static void join_after_backoff_work_handler(struct k_work *work);
K_TIMER_DEFINE(join_after_backoff_timer, join_after_backoff_timer_expiry, NULL);
K_WORK_DEFINE(join_after_backoff_work, join_after_backoff_work_handler);

/* k_timer expiry runs in ISR context on Zephyr; only submit work here. */
static void join_after_backoff_timer_expiry(struct k_timer *timer) {
  (void)timer;
  (void)k_work_submit(&join_after_backoff_work);
}

static void join_after_backoff_work_handler(struct k_work *work) {
  (void)work;
  LOG_INF("Link backoff elapsed; posting LORA_CMD_JOIN_SILENT");
  /* Customer-facing rejoin: no join LED (installer LED only for deliberate join
   * or first silent join right after power-on). */
  atomic_set(&silent_join_installer_led_armed, 0);
  int ret;
  for (int attempt = 0; attempt < 5; attempt++) {
    ret = lora_cmd_put(LORA_CMD_JOIN_SILENT);
    if (ret == 0) {
      return;
    }
    LOG_WRN("Rejoin cmd queue full (attempt %d/5), retry in 500ms",
            attempt + 1);
    k_msleep(500);
  }
  LOG_ERR("Failed to post rejoin command after 5 attempts; scheduling retry in "
          "30s");
  k_timer_start(&join_after_backoff_timer, K_SECONDS(30), K_NO_WAIT);
}

static void lora_log_join_ids(void) {
  LOG_INF("LoRaWAN OTAA identifiers in use:");
  LOG_HEXDUMP_INF(dev_eui, sizeof(dev_eui), "DevEUI");
  LOG_HEXDUMP_INF(join_eui, sizeof(join_eui), "JoinEUI");
}

static int lora_send_helper(uint8_t port, uint8_t *data, size_t len,
                            bool confirmed) {
  int ret;

  if (data == NULL || len == 0 || len > LORA_MAX_PAYLOAD_SIZE) {
    LOG_ERR("Invalid parameters: data=%p, len=%zu", data, len);
    return -EINVAL;
  }

  LOG_INF("UL port %u len %zu confirmed=%d", (unsigned)port, len,
          confirmed ? 1 : 0);
  LOG_HEXDUMP_INF(data, len, "UL FRMPayload");

#if EPD_ENABLED
  if (confirmed) {
    /* LoRa and EPD share SPI (see join path). Let any in-flight EPD transfer
     * finish before TX+RX1/RX2 so the radio stack is not starved during MAC
     * ACK receive (avoids spurious -ETIMEDOUT / "Rx 2 timeout"). */
    display_wait_until_spi_idle(5000);
  }
#endif

  for (int attempt = 0; attempt < 5; attempt++) {
    ret = lorawan_send(
        port, (uint8_t *)data, (uint8_t)len,
        confirmed ? LORAWAN_MSG_CONFIRMED : LORAWAN_MSG_UNCONFIRMED);

    if (ret == -EBUSY || ret == -EAGAIN) {
      if (attempt < 4) {
        LOG_DBG("lorawan_send busy, retry %d/5 in 500ms", attempt + 1);
        k_msleep(500);
      } else {
        LOG_WRN("lorawan_send: busy after 5 retries");
      }
    } else {
      break;
    }
  }

  if (ret == 0) {
    LOG_INF("UL OK port %u len %zu", (unsigned)port, len);
  } else if (ret < 0) {
    LOG_ERR("UL lorawan_send failed port=%u: %d", (unsigned)port, ret);
  }

  return ret;
}

#if LORA_POST_JOIN_MAC_PROBE_RETRIES > 0
/**
 * Confirm stack can run MCPS after join API returns. Empty uplink via LinkCheck.
 *
 * Side effect: each successful request emits a LinkCheckReq in an empty MCPS
 * frame; any LinkCheckAns returned in RX1/RX2 lands in lora_link_stats via the
 * MAC callback. We pause LORA_POST_JOIN_ANS_SETTLE_MS after a request returns
 * 0 so the Ans has time to arrive before the probe loop tears down / the
 * thread moves on.
 */
static bool lora_mac_probe_after_join(void) {
  for (int i = 0; i < LORA_POST_JOIN_MAC_PROBE_RETRIES; i++) {
    int sret = lorawan_request_link_check(true);
    if (sret == 0) {
      last_uplink_ms = (uint32_t)k_uptime_get();
      LOG_INF("Post-join MAC probe OK (attempt %d)", i + 1);
#if LORA_POST_JOIN_ANS_SETTLE_MS > 0
      /* Let RX1/RX2 deliver the LinkCheckAns so lora_link_stats updates before
       * callers read the snapshot (Stage 3 install screen). Safe no-op if the
       * Ans never arrives. */
      k_msleep(LORA_POST_JOIN_ANS_SETTLE_MS);
#endif
      lora_link_stats_snapshot_t ls;
      if (lora_link_stats_get(&ls) && ls.samples > 0) {
        LOG_INF("[LINK] post-join stats: last margin=%d dB gw=%u "
                "best margin=%d dB gw=%u samples=%u",
                (int)ls.last_demod_margin, (unsigned)ls.last_nb_gateways,
                (int)ls.best_demod_margin, (unsigned)ls.best_nb_gateways,
                (unsigned)ls.samples);
      } else {
        LOG_INF("[LINK] post-join: probe OK but no LinkCheckAns yet");
      }
      return true;
    }
    if (sret == -ENOTCONN) {
      LOG_WRN("Post-join MAC probe: stack not joined (attempt %d)", i + 1);
      return false;
    }
    LOG_DBG("Post-join MAC probe: err %d (attempt %d)", sret, i + 1);
    k_msleep(LORA_POST_JOIN_MAC_PROBE_RETRY_MS);
  }
  LOG_WRN("Post-join MAC probe: exhausted retries");
  return false;
}
#endif

/**
 * Run one join "cycle": up to LORA_JOIN_ATTEMPTS_PER_CYCLE attempts with
 * LORA_JOIN_RETRY_DELAY_SECONDS between them. Caller handles backoff (every
 * LORA_JOIN_BACKOFF_HOURS) when this returns false.
 *
 * @return true if joined, false if all attempts in this cycle failed.
 * @param show_join_led true: joining / success / fail-off LED for installer
 *                      (deliberate join, or first silent join after power-on).
 *                      false: silent background join (e.g. after link backoff).
 */
static bool run_join_cycle(struct lorawan_join_config *join_cfg,
                           bool show_join_led) {
  int ret;
  uint16_t dev_nonce;

  /* JOIN_STARTED is posted by caller only for deliberate join (LORA_CMD_JOIN).
   */
  LOG_SECTION_INF("STARTING LORA JOIN LOOP");
  LOG_INF("Up to %d attempts this cycle", LORA_JOIN_ATTEMPTS_PER_CYCLE);
  if (show_join_led) {
    (void)led_manager_show(
        0, LED_PATTERN_JOINING);
  }

  for (int attempt = 0; attempt < LORA_JOIN_ATTEMPTS_PER_CYCLE; attempt++) {
    /* Hold 3.3A (and 3.3V) for devnonce read and for the whole join attempt so
     * the LoRa radio can receive JoinAccept in Rx windows. On boot, main()
     * keeps rails on until enter_idle(); on retry after backoff, rails are off
     * so we must request here and release only after lorawan_join() returns. */
    rail_manager_request_3v3a();
    ret = devnonce_store_next(&dev_nonce);
    if (ret != 0) {
      rail_manager_release_3v3a();
      LOG_ERR("DevNonce allocation failed (%d) - rebooting", ret);
      sys_reboot(SYS_REBOOT_COLD);
    }
    join_cfg->otaa.dev_nonce = dev_nonce;
    LOG_INF("");
    LOG_INF("=== JOIN ATTEMPT %d / %d ===", attempt + 1,
            LORA_JOIN_ATTEMPTS_PER_CYCLE);
    lora_log_join_ids();
    LOG_INF("Joining network using OTAA, devNonce: %d", dev_nonce);
#if EPD_ENABLED
    /* LoRa and EPD share SPI; wait until EPD is not transferring so JoinAccept
     * RX windows can use the radio without SPI mutex contention. */
    display_wait_until_spi_idle(15000);
#endif
    ret = lorawan_join(join_cfg);
    LOG_INF("lorawan_join() returned: %d", ret);

    if (ret == 0) {
      /* Set SF7/125 before probe / time sync: DR0 downlinks often fail on some
       * gateways. ADR disabled at init; re-enabled after time sync completes.
       */
      int dr_ret = lorawan_set_datarate(LORA_TIME_SYNC_DR);
      if (dr_ret == 0) {
        LOG_INF("DR set to %s (SF7/125) for time sync; ADR off until sync done",
                LORA_TIME_SYNC_DR_STR);
      } else if (dr_ret != -EINVAL) {
        /* -EINVAL = already at target DR (e.g. join set DR5) */
        LOG_WRN("lorawan_set_datarate(DR=" LORA_TIME_SYNC_DR_STR ") failed: %d",
                dr_ret);
      }
#if LORA_POST_JOIN_MAC_PROBE_RETRIES > 0
      LOG_INF("Verifying MAC can transmit after join API success...");
      if (!lora_mac_probe_after_join()) {
        rail_manager_release_3v3a();
        LOG_WRN("Join API OK but MAC TX not ready; retrying OTAA");
        if (attempt + 1 < LORA_JOIN_ATTEMPTS_PER_CYCLE) {
          k_sleep(LORA_JOIN_RETRY_DELAY);
        }
        continue;
      }
#endif
      rail_manager_release_3v3a();

      LOG_SECTION_INF("LORA JOIN SUCCESSFUL");
      consecutive_send_failures = 0;
      k_timer_stop(&join_after_backoff_timer);
      atomic_set(&lora_joined_flag, 1);
      lora_on_join_success();
      k_sem_give(&lora_join_sem);

#if LORA_POST_JOIN_MAC_SETTLE_MS > 0
      k_msleep(LORA_POST_JOIN_MAC_SETTLE_MS);
#endif

      /* Join-success LED before JOINED so the burst starts first; SMF delays
       * LAST_CLEANED when installer_led (see POST_JOIN_LED_BEFORE_EPD_MS). */
      rail_manager_request_3v3a();
      if (show_join_led) {
        (void)led_manager_show(0, LED_PATTERN_JOIN_SUCCESS);
      }
      (void)smf_post_event(SMF_EVT_JOINED, show_join_led ? 1u : 0u,
                           k_uptime_get());
      (void)join_state_store_set_has_joined_once();
      rail_manager_release_3v3a();
      LOG_SECTION_INF("LORA JOIN LOOP COMPLETED SUCCESSFULLY");
      return true;
    }

    rail_manager_release_3v3a();

    if (ret == -ETIMEDOUT) {
      LOG_WRN("Join timed out - will retry in %d seconds",
              LORA_JOIN_RETRY_DELAY_SECONDS);
    } else {
      LOG_ERR("Join failed (%d) - will retry in %d seconds", ret,
              LORA_JOIN_RETRY_DELAY_SECONDS);
    }

    if (attempt + 1 < LORA_JOIN_ATTEMPTS_PER_CYCLE) {
      LOG_INF("Sleeping for %d seconds before next attempt...",
              LORA_JOIN_RETRY_DELAY_SECONDS);
      k_sleep(LORA_JOIN_RETRY_DELAY);
    }
  }

  LOG_WRN("Join failed after %d attempts (assume no gateway / genuine failure)",
          LORA_JOIN_ATTEMPTS_PER_CYCLE);
  if (show_join_led) {
    (void)led_manager_show(0, LED_PATTERN_OFF); /* stop joining blink */
  }
  return false;
}

static void lora_thread_fn(void *a, void *b, void *c) {
  int ret;
  uint8_t cmd;

  LOG_SECTION_INF("LORA THREAD ENTRY");
  k_sem_give(&lora_ready_sem);
  LOG_INF("LoRa thread started - Thread ID: %p", k_current_get());
  LOG_INF("LoRa thread priority: %d", k_thread_priority_get(k_current_get()));
  LOG_INF("LoRa thread stack size: %d", LORA_THREAD_STACK_SIZE);

  struct lorawan_join_config join_cfg = {.mode = LORAWAN_ACT_OTAA,
                                         .dev_eui = dev_eui,
                                         .otaa.join_eui = join_eui,
                                         .otaa.app_key = app_key,
                                         .otaa.nwk_key = app_key,
                                         .otaa.dev_nonce = 0};

  /* Join is command-driven: first boot waits for SMF (Staff+COMBO_JOIN);
   * subsequent boot auto-posts LORA_CMD_JOIN_SILENT (no Connecting screen;
   * join LED once for installer, not on backoff rejoin).
   * join_state_store is inited from main before this thread starts. */
  bool has_joined_once = false;
  (void)join_state_store_has_joined_once(&has_joined_once);
  LOG_INF("has_joined_once=%d (EEPROM_JOIN_STATE_CLEAR_ON_BOOT=%d)",
          has_joined_once, EEPROM_JOIN_STATE_CLEAR_ON_BOOT);
  if (has_joined_once) {
    /* One join cycle with LED after power-on so installers see progress without
     * logs; backoff rejoin clears this flag before posting JOIN_SILENT. */
    (void)atomic_set(&silent_join_installer_led_armed, 1);
    (void)lora_cmd_put(
        LORA_CMD_JOIN_SILENT); /* Auto-join: no Connecting screen */
  } else {
    LOG_SECTION_INF("FIRST BOOT: waiting for join command (Staff + 0+1+2)");
  }
  (void)lora_get_cmd(&cmd, K_FOREVER);
  LOG_INF("Join command received (cmd=%u), starting join cycle...", cmd);
  if (cmd == LORA_CMD_JOIN) {
    (void)smf_post_event(SMF_EVT_JOIN_STARTED, 0, k_uptime_get());
  }

  while (!run_join_cycle(&join_cfg, join_installer_led_for_cmd(cmd))) {
    (void)smf_post_event(SMF_EVT_JOIN_CYCLE_FAILED, 0, k_uptime_get());
    LOG_WRN("Will retry join in %d hour(s)", (int)LORA_JOIN_BACKOFF_HOURS);
    if (LORA_JOIN_BACKOFF_HOURS > 0) {
      k_sleep(K_HOURS(LORA_JOIN_BACKOFF_HOURS));
    } else {
      k_sleep(K_MINUTES(1)); /* 0 hours = test: 1 minute */
    }
  }

  /* Message loop: service command queue and uplink queue. SMF never calls
   * lorawan_*; it only posts commands (join, time_sync) or uplink via
   * lora_put_event. This thread owns all LoRaWAN and time_sync calls. */
  LOG_SECTION_INF("LORA THREAD ENTERING MESSAGE LOOP");
  struct k_poll_event events[2];
  struct k_poll_event *ev_msgq = &events[0];
  struct k_poll_event *ev_cmdq = &events[1];

  k_poll_event_init(ev_msgq, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                    K_POLL_MODE_NOTIFY_ONLY, &lora_msgq);
  k_poll_event_init(ev_cmdq, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                    K_POLL_MODE_NOTIFY_ONLY, &lora_cmdq);

  while (1) {
    ret = k_poll(events, 2, K_FOREVER);
    if (ret != 0) {
      LOG_ERR("k_poll failed: %d", ret);
      continue;
    }

    if (ev_msgq->state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
      lora_uplink_msg_t msg = {0};
      if (lora_get_event(&msg, K_NO_WAIT)) {
        if (msg.len > 0 && msg.len <= LORA_MAX_PAYLOAD_SIZE) {
          /* Rate limit: ensure min interval between uplinks (LoRa Alliance /
           * duty cycle). Wait if we sent too recently. */
          if (LORA_UPLINK_MIN_INTERVAL_MS > 0) {
            uint32_t now_ms = (uint32_t)k_uptime_get();
            uint32_t elapsed = now_ms - last_uplink_ms;
            if (last_uplink_ms != 0 &&
                elapsed < (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS) {
              uint32_t wait_ms =
                  (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS - elapsed;
              LOG_DBG("LoRa rate limit: wait %u ms", (unsigned)wait_ms);
              k_msleep(wait_ms);
            }
          }
          ret = lora_send_helper(msg.port, msg.data, msg.len, msg.confirmed);
          if (ret == 0) {
            last_uplink_ms = (uint32_t)k_uptime_get();
            consecutive_send_failures = 0;
          } else if (ret < 0) {
            LOG_ERR("Failed to send LoRa message: %d", ret);
            if (ret == -ENOTCONN) {
              if (lora_is_joined()) {
                atomic_set(&lora_joined_flag, 0);
                time_sync_abort_on_link_lost();
                lora_reset_dr_time_sync_retry();
                (void)smf_post_event(SMF_EVT_DISCONNECTED, 0, k_uptime_get());
                k_timeout_t backoff = LORA_JOIN_BACKOFF_HOURS > 0
                                          ? K_HOURS(LORA_JOIN_BACKOFF_HOURS)
                                          : K_MINUTES(1);
                k_timer_start(&join_after_backoff_timer, backoff, K_NO_WAIT);
                LOG_INF("Stack not joined (-ENOTCONN); cleared flag, re-join "
                        "in backoff");
              }
            } else if (ret != -EBUSY && ret != -EAGAIN) {
              /* Only count confirmed uplink failures (-116 Rx timeout) toward
               * "link lost". Unconfirmed: no ACK expected, -116 is benign. */
              if (msg.confirmed) {
                consecutive_send_failures++;
                if (LORA_SEND_FAILURES_BEFORE_BACKOFF > 0 &&
                    consecutive_send_failures >=
                        (uint32_t)LORA_SEND_FAILURES_BEFORE_BACKOFF &&
                    lora_is_joined()) {
                  LOG_WRN(
                      "Link lost: %u consecutive confirmed send failures; "
                      "clearing joined, scheduling re-join after backoff",
                      consecutive_send_failures);
                  atomic_set(&lora_joined_flag, 0);
                  time_sync_abort_on_link_lost();
                  lora_reset_dr_time_sync_retry();
                  consecutive_send_failures = 0;
                  (void)smf_post_event(SMF_EVT_DISCONNECTED, 0, k_uptime_get());
                  k_timeout_t backoff = LORA_JOIN_BACKOFF_HOURS > 0
                                            ? K_HOURS(LORA_JOIN_BACKOFF_HOURS)
                                            : K_MINUTES(1);
                  k_timer_start(&join_after_backoff_timer, backoff, K_NO_WAIT);
                }
              }
            }
          }
        } else {
          LOG_ERR("Invalid message length: %d", msg.len);
        }
      }
      k_poll_event_init(ev_msgq, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                        K_POLL_MODE_NOTIFY_ONLY, &lora_msgq);
    }

    if (ev_cmdq->state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
      if (lora_get_cmd(&cmd, K_NO_WAIT)) {
        if (cmd == LORA_CMD_JOIN || cmd == LORA_CMD_JOIN_SILENT) {
          if (cmd == LORA_CMD_JOIN) {
            (void)smf_post_event(SMF_EVT_JOIN_STARTED, 0, k_uptime_get());
          }
          if (!run_join_cycle(&join_cfg, join_installer_led_for_cmd(cmd))) {
            /* Join cycle failed (e.g. 20 attempts); schedule retry after
             * backoff. */
            k_timeout_t backoff = LORA_JOIN_BACKOFF_HOURS > 0
                                      ? K_HOURS(LORA_JOIN_BACKOFF_HOURS)
                                      : K_MINUTES(1);
            k_timer_start(&join_after_backoff_timer, backoff, K_NO_WAIT);
            LOG_INF("Join cycle failed; will retry in %s",
                    LORA_JOIN_BACKOFF_HOURS > 0 ? "hours" : "1 min");
          }
        } else if (cmd == LORA_CMD_TIME_SYNC) {
          time_sync_request_and_update_rtc();
        } else if (cmd == LORA_CMD_TIME_SYNC_RETRY) {
          time_sync_retry_request();
        } else if (cmd == LORA_CMD_ENABLE_ADR) {
          lorawan_enable_adr(true);
          LOG_INF("ADR re-enabled (time sync done; network will manage DR)");
        } else if (cmd == LORA_CMD_LINK_CHECK ||
                   cmd == LORA_CMD_LINK_CHECK_FORCE) {
          bool force = (cmd == LORA_CMD_LINK_CHECK_FORCE);
          (void)lorawan_request_link_check(force);
        }
      }
      k_poll_event_init(ev_cmdq, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                        K_POLL_MODE_NOTIFY_ONLY, &lora_cmdq);
    }
  }
}

// Define the thread but don't auto-start it (delay = -1 means don't auto-start)
K_THREAD_DEFINE(lora_thread_id, LORA_THREAD_STACK_SIZE, lora_thread_fn, NULL,
                NULL, NULL, LORA_THREAD_PRIORITY, 0, -1);
