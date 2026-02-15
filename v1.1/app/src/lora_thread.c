#include "devnonce_store.h"
#include "join_state_store.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
#include "rail_manager.h"
#include "smf_system_mode.h"
#include "sys_config.h"
#include "time_sync.h"
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(lora_thread, CONFIG_LOG_DEFAULT_LEVEL);

#define LORA_JOIN_RETRY_DELAY K_SECONDS(LORA_JOIN_RETRY_DELAY_SECONDS)

static uint8_t dev_eui[] = LORAWAN_DEV_EUI;
static uint8_t join_eui[] = LORAWAN_JOIN_EUI;
static uint8_t app_key[] = LORAWAN_APP_KEY;

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

  /* LoRa runs on 1.8V only; 3.6V is for NFC RF and not used for LoRa TX/RX. */

  LOG_INF("Sending payload (port %d, len %zu):", port, len);
  LOG_HEXDUMP_INF(data, len, "");

  ret =
      lorawan_send(port, (uint8_t *)data, (uint8_t)len,
                   confirmed ? LORAWAN_MSG_CONFIRMED : LORAWAN_MSG_UNCONFIRMED);

  if (ret == -EAGAIN) {
    LOG_WRN("lorawan_send: busy / too long");
  } else if (ret < 0) {
    LOG_ERR("lorawan_send failed: %d", ret);
  } else {
    LOG_INF("Data sent on port %d", port);
  }

  return ret;
}

#define TIME_SYNC_WAIT_TIMEOUT K_SECONDS(15)

/**
 * Run one join "cycle": up to LORA_JOIN_ATTEMPTS_PER_CYCLE attempts with
 * LORA_JOIN_RETRY_DELAY_SECONDS between them. Caller handles backoff (every
 * LORA_JOIN_BACKOFF_HOURS) when this returns false.
 *
 * @return true if joined, false if all attempts in this cycle failed.
 */
static bool run_join_cycle(struct lorawan_join_config *join_cfg) {
  int ret;
  uint16_t dev_nonce;

  /* JOIN_STARTED is posted by caller only for deliberate join (LORA_CMD_JOIN). */
  LOG_SECTION_INF("STARTING LORA JOIN LOOP");
  LOG_INF("Up to %d attempts this cycle", LORA_JOIN_ATTEMPTS_PER_CYCLE);

  for (int attempt = 0; attempt < LORA_JOIN_ATTEMPTS_PER_CYCLE; attempt++) {
    rail_manager_request_3v3a();
    ret = devnonce_store_next(&dev_nonce);
    rail_manager_release_3v3a();
    if (ret != 0) {
      LOG_ERR("DevNonce allocation failed (%d) - rebooting", ret);
      sys_reboot(SYS_REBOOT_COLD);
    }
    join_cfg->otaa.dev_nonce = dev_nonce;
    LOG_INF("");
    LOG_INF("=== JOIN ATTEMPT %d / %d ===", attempt + 1,
            LORA_JOIN_ATTEMPTS_PER_CYCLE);
    lora_log_join_ids();
    LOG_INF("Joining network using OTAA, devNonce: %d", dev_nonce);
    ret = lorawan_join(join_cfg);
    LOG_INF("lorawan_join() returned: %d", ret);

    if (ret == 0) {
      LOG_SECTION_INF("LORA JOIN SUCCESSFUL");
      atomic_set(&lora_joined_flag, 1);
      k_sem_give(&lora_join_sem);
      (void)smf_post_event(SMF_EVT_JOINED, 0, k_uptime_get());
      rail_manager_request_3v3a();
      (void)led_manager_show(0, LED_PATTERN_JOIN_SUCCESS);
      (void)join_state_store_set_has_joined_once();
      time_sync_request_and_update_rtc();
      rail_manager_release_3v3a();
      LOG_SECTION_INF("LORA JOIN LOOP COMPLETED SUCCESSFULLY");
      return true;
    }

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
  return false;
}

static void lora_thread_fn(void *a, void *b, void *c) {
  int ret;
  uint8_t cmd;

  LOG_SECTION_INF("LORA THREAD ENTRY");
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
   * subsequent boot auto-posts LORA_CMD_JOIN_SILENT (no Connecting screen).
   * join_state_store is inited from main before this thread starts. */
  bool has_joined_once = false;
  (void)join_state_store_has_joined_once(&has_joined_once);
  LOG_INF("has_joined_once=%d (EEPROM_JOIN_STATE_CLEAR_ON_BOOT=%d)",
          has_joined_once, EEPROM_JOIN_STATE_CLEAR_ON_BOOT);
  if (has_joined_once) {
    (void)lora_cmd_put(LORA_CMD_JOIN_SILENT); /* Auto-join: no Connecting screen */
  } else {
    LOG_SECTION_INF("FIRST BOOT: waiting for join command (Staff + 0+1+2)");
  }
  (void)lora_get_cmd(&cmd, K_FOREVER);
  LOG_INF("Join command received (cmd=%u), starting join cycle...", cmd);
  if (cmd == LORA_CMD_JOIN) {
    (void)smf_post_event(SMF_EVT_JOIN_STARTED, 0, k_uptime_get());
  }

  while (!run_join_cycle(&join_cfg)) {
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
          ret = lora_send_helper(msg.port, msg.data, msg.len, msg.confirmed);
          if (ret < 0) {
            LOG_ERR("Failed to send LoRa message: %d", ret);
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
          (void)run_join_cycle(&join_cfg);
        } else if (cmd == LORA_CMD_TIME_SYNC) {
          time_sync_request_and_update_rtc();
          int ts_ret = time_sync_wait(TIME_SYNC_WAIT_TIMEOUT);
          (void)smf_post_event(SMF_EVT_TIME_SYNC_DONE, (ts_ret == 0 ? 0 : 1),
                               k_uptime_get());
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
