#include "devnonce_store.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
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

  // Validate input parameters
  if (data == NULL || len == 0 || len > LORA_MAX_PAYLOAD_SIZE) {
    LOG_ERR("Invalid parameters: data=%p, len=%zu", data, len);
    return -EINVAL;
  }

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

static void lora_thread_fn(void *a, void *b, void *c) {
  int ret;
  uint16_t dev_nonce;

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

  // Try to join until success
  int attempt = 0;
  LOG_SECTION_INF("STARTING LORA JOIN LOOP");
  do {
    /* Allocate a monotonic, persisted DevNonce (prevents reuse across reboots).
     */
    ret = devnonce_store_next(&dev_nonce);
    if (ret != 0) {
      LOG_ERR("DevNonce allocation failed (%d) - rebooting", ret);
      sys_reboot(SYS_REBOOT_COLD);
    }
    join_cfg.otaa.dev_nonce = dev_nonce;
    LOG_INF("");
    LOG_INF("=== JOIN ATTEMPT %d ===", attempt);
    lora_log_join_ids();
    LOG_INF("Joining network using OTAA, devNonce: %d; attempt: %d", dev_nonce,
            attempt++);
    ret = lorawan_join(&join_cfg);
    LOG_INF("lorawan_join() returned: %d", ret);

    if (ret == -ETIMEDOUT) {
      LOG_WRN("Join timed out - will retry in %d seconds",
              LORA_JOIN_RETRY_DELAY_SECONDS);
    } else if (ret < 0) {
      LOG_ERR("Join failed (%d) - will retry in %d seconds", ret,
              LORA_JOIN_RETRY_DELAY_SECONDS);
    } else {
      LOG_SECTION_INF("LORA JOIN SUCCESSFUL");
      /* Mark as joined and signal waiting threads */
      atomic_set(&lora_joined_flag, 1);
      k_sem_give(&lora_join_sem);

      /* Visual feedback: blink status LED 5 times on successful join */
      for (int i = 0; i < 5; i++) {
        (void)led_manager_set_led(0, true);
        k_sleep(K_MSEC(200));
        (void)led_manager_set_led(0, false);
        k_sleep(K_MSEC(200));
      }

      /* Request network time and program RTC when DeviceTimeAns arrives */
      time_sync_request_and_update_rtc();
    }

    if (ret != 0) {
      LOG_INF("Sleeping for %d seconds before retry...",
              LORA_JOIN_RETRY_DELAY_SECONDS);
      k_sleep(LORA_JOIN_RETRY_DELAY);
    }

  } while (ret != 0);

  LOG_SECTION_INF("LORA JOIN LOOP COMPLETED SUCCESSFULLY");

  LOG_SECTION_INF("LORA THREAD ENTERING MESSAGE LOOP");
  while (1) {
    lora_uplink_msg_t msg = {0}; // Zero-initialize to prevent garbage data

    LOG_DBG("LoRa thread waiting for events...");
    if (lora_get_event(&msg, K_FOREVER)) {
      LOG_INF("");
      LOG_INF("=== LORA THREAD PROCESSING EVENT ===");

      // Additional validation before sending
      if (msg.len > 0 && msg.len <= LORA_MAX_PAYLOAD_SIZE) {
        ret = lora_send_helper(msg.port, msg.data, msg.len, msg.confirmed);
        if (ret < 0) {
          LOG_ERR("Failed to send LoRa message: %d", ret);
        }
      } else {
        LOG_ERR("Invalid message length: %d", msg.len);
      }
    }
  }
}

// Define the thread but don't auto-start it (delay = -1 means don't auto-start)
K_THREAD_DEFINE(lora_thread_id, LORA_THREAD_STACK_SIZE, lora_thread_fn, NULL,
                NULL, NULL, LORA_THREAD_PRIORITY, 0, -1);
