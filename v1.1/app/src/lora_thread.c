#include "devnonce_store.h"
#include "display_manager.h"
#include "join_state_store.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
#include "lora_link_stats.h"
#include "mapek_link.h"
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

static uint8_t dev_eui[] = LORAWAN_DEV_EUI;
static uint8_t join_eui[] = LORAWAN_JOIN_EUI;
static uint8_t app_key[] = LORAWAN_APP_KEY;
static bool boot_dev_eui_logged;

/* Last uplink completion time (ms) for rate limiting; 0 = never sent yet */
static uint32_t last_uplink_ms;

/** Enforce LORA_UPLINK_MIN_INTERVAL_MS since last successful app MCPS send.
 * MAC-only paths (LinkCheck, some join traffic) are not stamped here; see
 * pacing before lorawan_request_link_check. EU868 duty-cycle compliance is
 * still primarily enforced inside the LoRaMac regional layer when enabled. */
static void lora_pace_uplink_spacing(void) {
  if (LORA_UPLINK_MIN_INTERVAL_MS <= 0) {
    return;
  }
  uint32_t now_ms = (uint32_t)k_uptime_get();
  uint32_t elapsed = now_ms - last_uplink_ms;
  if (last_uplink_ms != 0U &&
      elapsed < (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS) {
    uint32_t wait_ms = (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS - elapsed;
    LOG_DBG("LoRa rate limit: wait %u ms", (unsigned)wait_ms);
    k_msleep(wait_ms);
  }
}

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
  LOG_INF("join backoff expired; rejoin (silent)");
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
  LOG_DBG("OTAA EUIs:");
  LOG_HEXDUMP_DBG(dev_eui, sizeof(dev_eui), "DevEUI");
  LOG_HEXDUMP_DBG(join_eui, sizeof(join_eui), "JoinEUI");
}

/* Ensure installers can see DevEUI at boot with default INFO logging. */
static void lora_log_boot_dev_eui_once(void) {
  if (boot_dev_eui_logged) {
    return;
  }
  LOG_HEXDUMP_INF(dev_eui, sizeof(dev_eui), "Boot DevEUI");
  boot_dev_eui_logged = true;
}

static int lora_send_helper(uint8_t port, uint8_t *data, size_t len,
                            bool confirmed) {
  int ret;

  if (data == NULL || len == 0 || len > LORA_MAX_PAYLOAD_SIZE) {
    LOG_ERR("Invalid parameters: data=%p, len=%zu", data, len);
    mapek_link_feed_mcps_uplink(-EINVAL, (uint8_t)MAPEK_UL_MCPS_APP, port,
                                0U, confirmed);
    return -EINVAL;
  }

  LOG_DBG("UL port=%u len=%zu cfm=%d", (unsigned)port, len,
          confirmed ? 1 : 0);
  LOG_HEXDUMP_DBG(data, len, "UL FRMPayload");

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
        LOG_WRN("lorawan_send busy after 5 retries");
      }
    } else {
      break;
    }
  }

  if (ret == 0) {
    LOG_INF("UL OK p=%u len=%zu", (unsigned)port, len);
  } else if (ret < 0) {
    LOG_ERR("UL send failed port=%u: %d", (unsigned)port, ret);
  }

  mapek_link_feed_mcps_uplink(ret, (uint8_t)MAPEK_UL_MCPS_APP, port,
                              (uint8_t)len, confirmed);

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
    mapek_link_feed_mcps_uplink(sret, (uint8_t)MAPEK_UL_MCPS_LINK_CHECK, 0, 0,
                                false);
    if (sret == 0) {
      last_uplink_ms = (uint32_t)k_uptime_get();
      LOG_DBG("post-join probe OK (attempt %d)", i + 1);
#if LORA_POST_JOIN_ANS_SETTLE_MS > 0
      /* Let RX1/RX2 deliver the LinkCheckAns so lora_link_stats updates before
       * callers read the snapshot (Stage 3 install screen). Safe no-op if the
       * Ans never arrives. */
      k_msleep(LORA_POST_JOIN_ANS_SETTLE_MS);
#endif
      lora_link_stats_snapshot_t ls;
      if (lora_link_stats_get(&ls) && ls.samples > 0) {
        LOG_INF("post-join link: last_margin=%d dB gw=%u "
                "best_margin=%d dB gw=%u samples=%u",
                (int)ls.last_demod_margin, (unsigned)ls.last_nb_gateways,
                (int)ls.best_demod_margin, (unsigned)ls.best_nb_gateways,
                (unsigned)ls.samples);
      } else {
        LOG_WRN("post-join link: no LinkCheckAns (probe sent)");
      }
      return true;
    }
    if (sret == -ENOTCONN) {
      LOG_WRN("post-join probe: stack not joined (attempt %d)", i + 1);
      return false;
    }
    LOG_DBG("post-join probe: err %d (attempt %d)", sret, i + 1);
    k_msleep(LORA_POST_JOIN_MAC_PROBE_RETRY_MS);
  }
  LOG_WRN("post-join probe: exhausted retries");
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
  LOG_DBG("join loop start attempts=%d", LORA_JOIN_ATTEMPTS_PER_CYCLE);
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
    LOG_INF("join %d/%d devNonce=%u", attempt + 1, LORA_JOIN_ATTEMPTS_PER_CYCLE,
            (unsigned)dev_nonce);
    lora_log_join_ids();
#if EPD_ENABLED
    /* LoRa and EPD share SPI; wait until EPD is not transferring so JoinAccept
     * RX windows can use the radio without SPI mutex contention. */
    display_wait_until_spi_idle(15000);
#endif
    ret = lorawan_join(join_cfg);
    if (ret == 0) {
      LOG_DBG("lorawan_join ok");
    } else {
      LOG_INF("lorawan_join ret=%d", ret);
    }

    if (ret == 0) {

      lorawan_enable_adr(true);

      LOG_DBG("ADR on (post-join); DeviceTimeReq deferred");

#if LORA_POST_JOIN_MAC_PROBE_RETRIES > 0
      LOG_DBG("post-join MAC probe");
      if (!lora_mac_probe_after_join()) {
        rail_manager_release_3v3a();
        LOG_WRN("join OK but MAC TX not ready; retry OTAA");
        if (attempt + 1 < LORA_JOIN_ATTEMPTS_PER_CYCLE) {
          k_sleep(LORA_JOIN_RETRY_DELAY);
        }
        continue;
      }
#endif
      rail_manager_release_3v3a();

      LOG_INF("LoRaWAN joined");
      consecutive_send_failures = 0;
      k_timer_stop(&join_after_backoff_timer);
      atomic_set(&lora_joined_flag, 1);
      mapek_link_feed_join(true);
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
      LOG_DBG("join loop done");
      return true;
    }

    rail_manager_release_3v3a();

    if (ret == -ETIMEDOUT) {
      LOG_WRN("join timed out; retry in %d s",
              LORA_JOIN_RETRY_DELAY_SECONDS);
    } else {
      LOG_ERR("join failed (%d); retry in %d s", ret,
              LORA_JOIN_RETRY_DELAY_SECONDS);
    }

    if (attempt + 1 < LORA_JOIN_ATTEMPTS_PER_CYCLE) {
      LOG_DBG("sleep %d s until next attempt", LORA_JOIN_RETRY_DELAY_SECONDS);
      k_sleep(LORA_JOIN_RETRY_DELAY);
    }
  }

  LOG_WRN("join failed after %d attempts (no gateway or RF issue)",
          LORA_JOIN_ATTEMPTS_PER_CYCLE);
  if (show_join_led) {
    (void)led_manager_show(0, LED_PATTERN_OFF); /* stop joining blink */
  }
  return false;
}

static void lora_thread_fn(void *a, void *b, void *c) {
  int ret;
  uint8_t cmd;

  LOG_DBG("LoRa thread start");
  k_sem_give(&lora_ready_sem);
  LOG_INF("tid=%p prio=%d stk=%u", k_current_get(),
          k_thread_priority_get(k_current_get()), LORA_THREAD_STACK_SIZE);

  struct lorawan_join_config join_cfg = {.mode = LORAWAN_ACT_OTAA,
                                         .dev_eui = dev_eui,
                                         .otaa.join_eui = join_eui,
                                         .otaa.app_key = app_key,
                                         .otaa.nwk_key = app_key,
                                         .otaa.dev_nonce = 0};
  lora_log_boot_dev_eui_once();

  /* Join is command-driven: first boot waits for SMF (Staff+COMBO_JOIN);
   * subsequent boot auto-posts LORA_CMD_JOIN_SILENT (no Connecting screen;
   * join LED once for installer, not on backoff rejoin).
   * join_state_store is inited from main before this thread starts. */
  bool has_joined_once = false;
  (void)join_state_store_has_joined_once(&has_joined_once);
  LOG_INF("has_joined_once=%d clear_on_boot=%d", has_joined_once,
          EEPROM_JOIN_STATE_CLEAR_ON_BOOT);
  if (has_joined_once) {
    /* One join cycle with LED after power-on so installers see progress without
     * logs; backoff rejoin clears this flag before posting JOIN_SILENT. */
    (void)atomic_set(&silent_join_installer_led_armed, 1);
    (void)lora_cmd_put(
        LORA_CMD_JOIN_SILENT); /* Auto-join: no Connecting screen */
  } else {
    LOG_INF("first boot: wait staff combo join");
  }
  (void)lora_get_cmd(&cmd, K_FOREVER);
  LOG_INF("join cmd=%u starting cycle", cmd);
  if (cmd == LORA_CMD_JOIN) {
    (void)smf_post_event(SMF_EVT_JOIN_STARTED, 0, k_uptime_get());
  }

  while (!run_join_cycle(&join_cfg, join_installer_led_for_cmd(cmd))) {
    (void)smf_post_event(SMF_EVT_JOIN_CYCLE_FAILED, 0, k_uptime_get());
    LOG_WRN("join retry in %d h", (int)LORA_JOIN_BACKOFF_HOURS);
    if (LORA_JOIN_BACKOFF_HOURS > 0) {
      k_sleep(K_HOURS(LORA_JOIN_BACKOFF_HOURS));
    } else {
      k_sleep(K_MINUTES(1)); /* 0 hours = test: 1 minute */
    }
  }

  /* Message loop: service command queue and uplink queue. SMF never calls
   * lorawan_*; it only posts commands (join, time_sync) or uplink via
   * lora_put_event. This thread owns all LoRaWAN and time_sync calls. */
  LOG_DBG("message loop");
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
          /* Rate limit: fair use + spacing before next PHY burst (see
           * lora_pace_uplink_spacing). */
          lora_pace_uplink_spacing();
          ret = lora_send_helper(msg.port, msg.data, msg.len, msg.confirmed);
          if (ret == 0) {
            last_uplink_ms = (uint32_t)k_uptime_get();
            consecutive_send_failures = 0;
          } else if (ret < 0) {
            LOG_ERR("Failed to send LoRa message: %d", ret);
            if (ret == -ENOTCONN) {
              if (lora_is_joined()) {
                atomic_set(&lora_joined_flag, 0);
                mapek_link_feed_join(false);
                time_sync_abort_on_link_lost();
                lora_reset_dr_time_sync_retry();
                (void)smf_post_event(SMF_EVT_DISCONNECTED, 0, k_uptime_get());
                k_timeout_t backoff = LORA_JOIN_BACKOFF_HOURS > 0
                                          ? K_HOURS(LORA_JOIN_BACKOFF_HOURS)
                                          : K_MINUTES(1);
                k_timer_start(&join_after_backoff_timer, backoff, K_NO_WAIT);
                LOG_INF("ENOTCONN: cleared joined; rejoin after backoff");
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
                  mapek_link_feed_join(false);
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
            LOG_INF("join cycle failed; backoff retry scheduled");
          }
        } else if (cmd == LORA_CMD_TIME_SYNC) {
          /* DeviceTimeReq often rides the next MCPS uplink; spacing avoids
           * back-to-back PHY with the last app frame. */
          lora_pace_uplink_spacing();
          time_sync_request_and_update_rtc();
        } else if (cmd == LORA_CMD_TIME_SYNC_RETRY) {
          lora_pace_uplink_spacing();
          time_sync_retry_request();
        } else if (cmd == LORA_CMD_ENABLE_ADR) {
          lorawan_enable_adr(true);
          LOG_INF("ADR re-enabled (time sync done)");
        } else if (cmd == LORA_CMD_LINK_CHECK ||
                   cmd == LORA_CMD_LINK_CHECK_FORCE) {
          lora_pace_uplink_spacing();
          bool force = (cmd == LORA_CMD_LINK_CHECK_FORCE);
          int lc_ret = lorawan_request_link_check(force);
          mapek_link_feed_mcps_uplink(lc_ret, (uint8_t)MAPEK_UL_MCPS_LINK_CHECK,
                                      0, 0, false);
        }
      }
      k_poll_event_init(ev_cmdq, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                        K_POLL_MODE_NOTIFY_ONLY, &lora_cmdq);
    }

    mapek_link_step();
  }
}

// Define the thread but don't auto-start it (delay = -1 means don't auto-start)
K_THREAD_DEFINE(lora_thread_id, LORA_THREAD_STACK_SIZE, lora_thread_fn, NULL,
                NULL, NULL, LORA_THREAD_PRIORITY, 0, -1);
