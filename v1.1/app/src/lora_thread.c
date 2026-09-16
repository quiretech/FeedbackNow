#include "devnonce_store.h"
#include "display_manager.h"
#include "join_state_store.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
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
static void lora_pace_uplink_spacing(void)
{
  if (LORA_UPLINK_MIN_INTERVAL_MS <= 0)
  {
    return;
  }
  uint32_t now_ms = (uint32_t)k_uptime_get();
  uint32_t elapsed = now_ms - last_uplink_ms;
  if (last_uplink_ms != 0U &&
      elapsed < (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS)
  {
    uint32_t wait_ms = (uint32_t)LORA_UPLINK_MIN_INTERVAL_MS - elapsed;
    LOG_DBG("LoRa rate limit: wait %u ms", (unsigned)wait_ms);
    k_msleep(wait_ms);
  }
}

/* Next LORA_CMD_JOIN_SILENT may show join LED only when this is non-zero (set
 * for the auto-post right after boot when has_joined_once; cleared before
 * backoff rejoin and when handling LORA_CMD_JOIN). */
static atomic_t silent_join_installer_led_armed;

K_SEM_DEFINE(lora_ready_sem, 0, 1);

/* FLEXBOX status LED shares the 3.3A peripheral rail (see SMF Staff comments).
 * Hold that rail for the whole installer join campaign — not only during each
 * lorawan_join() call — so the LED stays powered in OTAA inter-attempt sleeps. */
static bool join_installer_rail_held;

static void join_installer_rail_hold(bool show_join_led)
{
  if (!show_join_led || join_installer_rail_held)
  {
    return;
  }
  rail_manager_request_3v3a();
  join_installer_rail_held = true;
}

static void join_installer_rail_release(void)
{
  if (!join_installer_rail_held)
  {
    return;
  }
  rail_manager_release_3v3a();
  join_installer_rail_held = false;
}

/** Join LED: deliberate join always; silent only for first cycle after boot
 *  auto-post (installer at power-on), never for backoff rejoin. */
static bool join_installer_led_for_cmd(uint8_t cmd)
{
  if (cmd == LORA_CMD_JOIN)
  {
    (void)atomic_set(&silent_join_installer_led_armed, 0);
    return true;
  }
  if (cmd == LORA_CMD_JOIN_SILENT)
  {
    return atomic_set(&silent_join_installer_led_armed, 0) != 0;
  }
  return false;
}

static void join_after_backoff_timer_expiry(struct k_timer *timer);
static void join_after_backoff_work_handler(struct k_work *work);
K_TIMER_DEFINE(join_after_backoff_timer, join_after_backoff_timer_expiry, NULL);
K_WORK_DEFINE(join_after_backoff_work, join_after_backoff_work_handler);

/* k_timer expiry runs in ISR context on Zephyr; only submit work here. */
static void join_after_backoff_timer_expiry(struct k_timer *timer)
{
  (void)timer;
  (void)k_work_submit(&join_after_backoff_work);
}

static k_timeout_t lora_join_backoff_timeout(void)
{
  return LORA_JOIN_BACKOFF_HOURS > 0 ? K_HOURS(LORA_JOIN_BACKOFF_HOURS)
                                     : K_MINUTES(1);
}

/** Clear joined, abort time sync, notify SMF, schedule silent rejoin. */
static void lora_session_lost_teardown(const char *reason)
{
  if (!lora_is_joined())
  {
    return;
  }
  LOG_WRN("LoRa session lost (%s)", reason);
  atomic_set(&lora_joined_flag, 0);
  atomic_set(&silent_join_installer_led_armed, 0);
  join_installer_rail_release();
  time_sync_abort_on_link_lost();
  lora_reset_dr_time_sync_retry();
  (void)smf_post_event(SMF_EVT_DISCONNECTED, 0, k_uptime_get());
  k_timer_start(&join_after_backoff_timer, lora_join_backoff_timeout(),
                K_NO_WAIT);
}

static void join_after_backoff_work_handler(struct k_work *work)
{
  (void)work;
  LOG_DBG("join backoff expired; rejoin (silent)");
  /* armed flag consumed by join_installer_led_for_cmd on the silent cycle. */
  int ret;
  for (int attempt = 0; attempt < 5; attempt++)
  {
    ret = lora_cmd_put(LORA_CMD_JOIN_SILENT);
    if (ret == 0)
    {
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

static void lora_log_join_ids(void)
{
  LOG_DBG("OTAA EUIs:");
  LOG_HEXDUMP_DBG(dev_eui, sizeof(dev_eui), "DevEUI");
  LOG_HEXDUMP_DBG(join_eui, sizeof(join_eui), "JoinEUI");
}

/* Ensure installers can see DevEUI at boot with default INFO logging. */
static void lora_log_boot_dev_eui_once(void)
{
  if (boot_dev_eui_logged)
  {
    return;
  }
  LOG_HEXDUMP_INF(dev_eui, sizeof(dev_eui), "Boot DevEUI");
  boot_dev_eui_logged = true;
}

/** Hold 3.3A for LoRa radio SPI/RF (FLEXBOX: same peripheral rail as SX1262). */
static void lora_radio_rail_hold(void)
{
  rail_manager_request_3v3a();
}

static void lora_radio_rail_release(void)
{
  rail_manager_release_3v3a();
}

static int lora_send_helper(uint8_t port, uint8_t *data, size_t len,
                            bool confirmed)
{
  int ret;

  if (data == NULL || len == 0 || len > LORA_MAX_PAYLOAD_SIZE)
  {
    LOG_ERR("Invalid parameters: data=%p, len=%zu", data, len);
    return -EINVAL;
  }

  LOG_DBG("UL port=%u len=%zu cfm=%d", (unsigned)port, len,
          confirmed ? 1 : 0);
  LOG_HEXDUMP_DBG(data, len, "UL FRMPayload");

#if EPD_ENABLED
  if (confirmed)
  {
    /* LoRa and EPD share SPI (see join path). Let any in-flight EPD transfer
     * finish before TX+RX1/RX2 so the radio stack is not starved during MAC
     * ACK receive (avoids spurious -ETIMEDOUT / "Rx 2 timeout"). */
    display_wait_until_spi_idle(5000);
  }
#endif

  lora_radio_rail_hold();
  lora_pace_uplink_spacing();

  for (int attempt = 0; attempt < 5; attempt++)
  {
    ret = lorawan_send(
        port, (uint8_t *)data, (uint8_t)len,
        confirmed ? LORAWAN_MSG_CONFIRMED : LORAWAN_MSG_UNCONFIRMED);

    if (ret == -EBUSY || ret == -EAGAIN)
    {
      if (attempt < 4)
      {
        LOG_INF("lorawan_send busy, retry %d/5 in 500ms", attempt + 1);
        k_msleep(500);
      }
      else
      {
        LOG_WRN("lorawan_send busy after 5 retries");
      }
    }
    else
    {
      break;
    }
  }

  lora_radio_rail_release();

  if (ret == 0)
  {
    LOG_INF("UL OK p=%u len=%zu", (unsigned)port, len);
  }
  else if (ret < 0)
  {
    LOG_ERR("UL send failed port=%u: %d", (unsigned)port, ret);
  }

  return ret;
}

#if LORA_POST_JOIN_MAC_PROBE_RETRIES > 0
/** Post-join SPI/MAC handoff: do not send LinkCheck here — RX2 overlaps LAST_CLEANED
 * EPD on shared SPI and corrupts join state (DeviceTime Busy, vote ENOTCONN). */
static bool lora_mac_probe_after_join(void)
{
#if EPD_ENABLED
  display_wait_until_spi_idle(5000);
#endif
  LOG_DBG("post-join: MAC deferred until after EPD");
  return true;
}
#endif

/** App-layer reset before Staff combo re-join. Zephyr has no lorawan_leave(). */
void lora_prepare_deliberate_rejoin(void)
{
  lora_uplink_msg_t drop;

  k_timer_stop(&join_after_backoff_timer);
  lora_cancel_scheduled_burst_time_sync();
  time_sync_abort_on_link_lost();
  lora_reset_dr_time_sync_retry();

  if (lora_is_joined())
  {
    LOG_DBG("deliberate rejoin: clear joined flag (fresh OTAA follows)");
    atomic_set(&lora_joined_flag, 0);
  }

  while (lora_get_event(&drop, K_NO_WAIT))
  {
    LOG_DBG("rejoin prep: drop stale uplink port=%u len=%u",
            (unsigned)drop.port, (unsigned)drop.len);
  }
}

/**
 * Run one join "cycle": up to LORA_JOIN_ATTEMPTS_PER_CYCLE attempts with
 * LORA_JOIN_RETRY_DELAY_SECONDS between them. Caller handles backoff (every
 * LORA_JOIN_BACKOFF_HOURS) when this returns false.
 *
 * @return true if joined, false if all attempts in this cycle failed.
 * @param show_join_led true: joining blink for installer (deliberate join,
 *                      first silent join after power-on, and through backoff).
 *                      false: silent background join (e.g. link-loss rejoin).
 * @param deliberate_rejoin true for LORA_CMD_JOIN (Staff combo): prep session.
 */
/** Re-post JOINING so blink survives long LoRa sleeps / thread starvation. */
static void join_led_show_active(bool show_join_led)
{
  if (show_join_led)
  {
    (void)led_manager_show(0, LED_PATTERN_JOINING);
  }
}

static bool run_join_cycle(struct lorawan_join_config *join_cfg,
                           bool show_join_led, bool deliberate_rejoin)
{
  int ret;
  uint16_t dev_nonce;

  /* CONNECTING EPD: SMF runs display_show_connecting_sync() before
   * lora_request_join(); do not post JOIN_STARTED from LoRa thread. */
  LOG_DBG("join loop start attempts=%d deliberate=%d",
          LORA_JOIN_ATTEMPTS_PER_CYCLE, (int)deliberate_rejoin);
  if (deliberate_rejoin)
  {
    lora_prepare_deliberate_rejoin();
  }
  if (show_join_led)
  {
    (void)led_manager_show(
        0, LED_PATTERN_JOINING);
  }
  join_installer_rail_hold(show_join_led);

  for (int attempt = 0; attempt < LORA_JOIN_ATTEMPTS_PER_CYCLE; attempt++)
  {
    /* Hold 3.3A (and 3.3V) for devnonce read and for the whole join attempt so
     * the LoRa radio can receive JoinAccept in Rx windows. On boot, main()
     * keeps rails on until enter_idle(); on retry after backoff, rails are off
     * so we must request here and release only after lorawan_join() returns. */
    rail_manager_request_3v3a();
    ret = devnonce_store_next(&dev_nonce);
    if (ret != 0)
    {
      rail_manager_release_3v3a();
      LOG_ERR("DevNonce allocation failed (%d) - rebooting", ret);
      sys_reboot(SYS_REBOOT_COLD);
    }
    join_cfg->otaa.dev_nonce = dev_nonce;
    LOG_STATE("join %d/%d devNonce=%u", attempt + 1, LORA_JOIN_ATTEMPTS_PER_CYCLE,
            (unsigned)dev_nonce);
    lora_log_join_ids();
#if EPD_ENABLED
    /* LoRa and EPD share SPI; wait until EPD is not transferring so JoinAccept
     * RX windows can use the radio without SPI mutex contention. */
    display_wait_until_spi_idle(15000);
#endif
    int64_t join_t0 = k_uptime_get();
    ret = lorawan_join(join_cfg);
    int64_t join_elapsed = k_uptime_get() - join_t0;
    if (ret == 0)
    {
      LOG_STATE("lorawan_join ok (%lld ms)", (long long)join_elapsed);
    }
    else
    {
      LOG_DBG("lorawan_join ret=%d (%lld ms)", ret, (long long)join_elapsed);
    }

    if (ret == 0)
    {
      if (join_elapsed < (int64_t)LORA_JOIN_SUSPICIOUS_MAX_MS)
      {
        LOG_WRN("join ok in %lld ms (stale MLME sem?); OTAA guard %u ms",
                (long long)join_elapsed, (unsigned)LORA_JOIN_OTAA_GUARD_MS);
        if ((uint32_t)join_elapsed < (uint32_t)LORA_JOIN_OTAA_GUARD_MS)
        {
#if EPD_ENABLED
          display_wait_until_spi_idle(5000);
#endif
          k_msleep((uint32_t)LORA_JOIN_OTAA_GUARD_MS - (uint32_t)join_elapsed);
        }
      }

      lorawan_enable_adr(true);

      LOG_DBG("ADR on (post-join); MAC probe then SMF DeviceTime");

#if LORA_POST_JOIN_MAC_PROBE_RETRIES > 0
      if (!lora_mac_probe_after_join())
      {
        rail_manager_release_3v3a();
        LOG_WRN("join OK but MAC TX not ready; retry OTAA");
        if (attempt + 1 < LORA_JOIN_ATTEMPTS_PER_CYCLE)
        {
          join_led_show_active(show_join_led);
          k_sleep(LORA_JOIN_RETRY_DELAY);
        }
        continue;
      }
#endif
      rail_manager_release_3v3a();

      LOG_EVT("LoRaWAN joined");
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
      if (show_join_led)
      {
        (void)led_manager_show(0, LED_PATTERN_JOIN_SUCCESS);
      }
      uint8_t joined_tag = 0;
      if (show_join_led)
      {
        joined_tag = deliberate_rejoin ? 2u : 1u;
      }
      (void)smf_post_event(SMF_EVT_JOINED, joined_tag, k_uptime_get());
      (void)join_state_store_set_has_joined_once();
      rail_manager_release_3v3a();
      LOG_STATE("join loop done");
      join_installer_rail_release();
      return true;
    }

    rail_manager_release_3v3a();

    if (ret == -ETIMEDOUT)
    {
      LOG_WRN("join timed out; retry in %d s",
              LORA_JOIN_RETRY_DELAY_SECONDS);
    }
    else
    {
      LOG_ERR("join failed (%d); retry in %d s", ret,
              LORA_JOIN_RETRY_DELAY_SECONDS);
    }

    if (attempt + 1 < LORA_JOIN_ATTEMPTS_PER_CYCLE)
    {
      LOG_DBG("sleep %d s until next attempt", LORA_JOIN_RETRY_DELAY_SECONDS);
      join_led_show_active(show_join_led);
      k_sleep(LORA_JOIN_RETRY_DELAY);
    }
  }

  LOG_WRN("join failed after %d attempts (no gateway or RF issue)",
          LORA_JOIN_ATTEMPTS_PER_CYCLE);
  /* Keep JOINING blink + 3.3A through inter-cycle backoff; release on success. */
  join_led_show_active(show_join_led);
  return false;
}

static void lora_thread_fn(void *a, void *b, void *c)
{
  int ret;
  uint8_t cmd;

  LOG_STATE("LoRa thread start");
  k_sem_give(&lora_ready_sem);
  LOG_DBG("tid=%p prio=%d stk=%u", k_current_get(),
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
  LOG_DBG("has_joined_once=%d clear_on_boot=%d", has_joined_once,
          EEPROM_JOIN_STATE_CLEAR_ON_BOOT);
  if (has_joined_once)
  {
    /* One join cycle with LED after power-on so installers see progress without
     * logs; backoff rejoin clears this flag before posting JOIN_SILENT. */
    (void)atomic_set(&silent_join_installer_led_armed, 1);
    (void)lora_cmd_put(
        LORA_CMD_JOIN_SILENT); /* Auto-join: no Connecting screen */
  }
  else
  {
    LOG_STATE("first boot: wait staff combo join");
  }
  (void)lora_get_cmd(&cmd, K_FOREVER);
  LOG_STATE("join cmd=%u starting cycle", cmd);

  const bool show_join_led_boot = join_installer_led_for_cmd(cmd);

  while (!run_join_cycle(&join_cfg, show_join_led_boot, cmd == LORA_CMD_JOIN))
  {
    (void)smf_post_event(SMF_EVT_JOIN_CYCLE_FAILED, 0, k_uptime_get());
    LOG_WRN("join retry in %d h", (int)LORA_JOIN_BACKOFF_HOURS);
    join_led_show_active(show_join_led_boot);
    if (LORA_JOIN_BACKOFF_HOURS > 0)
    {
      k_sleep(K_HOURS(LORA_JOIN_BACKOFF_HOURS));
    }
    else
    {
      k_sleep(K_MINUTES(1)); /* 0 hours = test: 1 minute */
    }
  }

  /* Message loop: service command queue and uplink queue. SMF never calls
   * lorawan_*; it only posts commands (join, time_sync) or uplink via
   * lora_put_event. Steady-state TX/RX holds 3.3A for the full send/MAC
   * session (see lora_send_helper). */
  LOG_DBG("message loop");
  struct k_poll_event events[2];
  struct k_poll_event *ev_msgq = &events[0];
  struct k_poll_event *ev_cmdq = &events[1];

  k_poll_event_init(ev_msgq, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                    K_POLL_MODE_NOTIFY_ONLY, &lora_msgq);
  k_poll_event_init(ev_cmdq, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                    K_POLL_MODE_NOTIFY_ONLY, &lora_cmdq);

  while (1)
  {
    ret = k_poll(events, 2, K_FOREVER);
    if (ret != 0)
    {
      LOG_ERR("k_poll failed: %d", ret);
      continue;
    }

    if (ev_msgq->state == K_POLL_STATE_MSGQ_DATA_AVAILABLE)
    {
      lora_uplink_msg_t msg = {0};
      if (lora_get_event(&msg, K_NO_WAIT))
      {
        if (msg.len > 0 && msg.len <= LORA_MAX_PAYLOAD_SIZE)
        {
          /* lora_send_helper: 3.3A hold + rate limit + lorawan_send (TX/RX). */
          ret = lora_send_helper(msg.port, msg.data, msg.len, msg.confirmed);
          if (ret == 0)
          {
            last_uplink_ms = (uint32_t)k_uptime_get();
          }
          else if (ret < 0)
          {
            LOG_ERR("Failed to send LoRa message: %d", ret);
            if (ret == -ENOTCONN)
            {
              lora_session_lost_teardown("ENOTCONN");
            }
          }
        }
        else
        {
          LOG_ERR("Invalid message length: %d", msg.len);
        }
      }
      k_poll_event_init(ev_msgq, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                        K_POLL_MODE_NOTIFY_ONLY, &lora_msgq);
    }

    if (ev_cmdq->state == K_POLL_STATE_MSGQ_DATA_AVAILABLE)
    {
      if (lora_get_cmd(&cmd, K_NO_WAIT))
      {
        if (cmd == LORA_CMD_JOIN || cmd == LORA_CMD_JOIN_SILENT)
        {
          const bool show_join_led = join_installer_led_for_cmd(cmd);
          if (!show_join_led)
          {
            join_installer_rail_release();
          }
          if (!run_join_cycle(&join_cfg, show_join_led, cmd == LORA_CMD_JOIN))
          {
            /* Deliberate Staff rejoin: keep installer LED on silent backoff retry. */
            if (cmd == LORA_CMD_JOIN)
            {
              (void)atomic_set(&silent_join_installer_led_armed, 1);
            }
            /* Join cycle failed (e.g. 20 attempts); schedule retry after
             * backoff. */
            k_timer_start(&join_after_backoff_timer, lora_join_backoff_timeout(),
                          K_NO_WAIT);
            LOG_STATE("join cycle failed; backoff retry scheduled");
          }
        }
        else if (cmd == LORA_CMD_TIME_SYNC)
        {
#if EPD_ENABLED
          display_wait_until_spi_idle(10000);
#endif
          lora_pace_uplink_spacing();
          time_sync_request_and_update_rtc();
        }
        else if (cmd == LORA_CMD_LINK_CHECK ||
                 cmd == LORA_CMD_LINK_CHECK_FORCE)
        {
          bool force = (cmd == LORA_CMD_LINK_CHECK_FORCE);
          lora_radio_rail_hold();
          lora_pace_uplink_spacing();
          (void)lorawan_request_link_check(force);
          /* Class-A MAC answer windows after LinkCheckReq */
          k_msleep(LORA_POST_JOIN_ANS_SETTLE_MS);
          lora_radio_rail_release();
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
