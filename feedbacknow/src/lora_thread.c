#include "lora_app.h"
#include "nfc_manager.h"
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>

LOG_MODULE_REGISTER(lora_thread, CONFIG_LOG_DEFAULT_LEVEL);

#define LORA_JOIN_RETRY_DELAY K_SECONDS(LORA_JOIN_RETRY_DELAY_SECONDS)
// Always use a random DevNonce; do not use NVS.

#include <zephyr/random/random.h>

static uint8_t dev_eui[] = LORAWAN_DEV_EUI;
static uint8_t join_eui[] = LORAWAN_JOIN_EUI;
static uint8_t app_key[] = LORAWAN_APP_KEY;

static uint16_t dev_nonce = 0;

static uint16_t generate_dev_nonce(void) {
  // Generate a DevNonce in the full 0..65535 range, maximizing randomness
  uint16_t dev_nonce;
  uint32_t rnd = 0;
  sys_rand_get(&rnd, sizeof(rnd));
  // Mix in uptime and cycle count for extra entropy
  rnd ^= k_uptime_get_32();
  rnd ^= k_cycle_get_32();
  // Use all 16 bits for DevNonce
  dev_nonce = (uint16_t)(rnd & 0xFFFF);
  return dev_nonce;
}

// K_MUTEX_DEFINE(lora_send_mutex);
static int lora_send_helper(uint8_t port, uint8_t *data, size_t len,
                            bool confirmed) {
  int ret;

  // Validate input parameters
  if (data == NULL || len == 0 || len > LORA_MAX_PAYLOAD_SIZE) {
    LOG_ERR("Invalid parameters: data=%p, len=%zu", data, len);
    return -EINVAL;
  }

  LOG_INF("Sending payload (port %d, len %d):", port, len);
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

  LOG_INF("LoRa thread started");
  // Use compile-time keys and random DevNonce; no NVS access.

  struct lorawan_join_config join_cfg = {.mode = LORAWAN_ACT_OTAA,
                                         .dev_eui = dev_eui,
                                         .otaa.join_eui = join_eui,
                                         .otaa.app_key = app_key,
                                         .otaa.nwk_key = app_key,
                                         .otaa.dev_nonce = 0};

  // Try to join until success
  int attempt = 0;
  do {
    // Generate a fresh random DevNonce for each join attempt
    dev_nonce = generate_dev_nonce();
    join_cfg.otaa.dev_nonce = dev_nonce;
    LOG_INF("Joining network using OTAA, devNonce: %d; attempt: %d", dev_nonce,
            attempt++);
    ret = lorawan_join(&join_cfg);

    if (ret == -ETIMEDOUT) {
      LOG_WRN("Join timed out");
    } else if (ret < 0) {
      LOG_ERR("Join failed (%d)", ret);
    } else {
      LOG_INF("Join successful");

      // Initialize NFC manager after successful LoRa join to avoid SPI
      // conflicts
      LOG_INF("Initializing NFC manager after LoRa join...");
      int nfc_ret = nfc_manager_init();
      if (nfc_ret != 0) {
        LOG_ERR("NFC manager initialization failed: %d", nfc_ret);
      } else {
        LOG_INF("NFC manager initialized successfully");
      }
    }

    if (ret != 0)
      k_sleep(LORA_JOIN_RETRY_DELAY);

  } while (ret != 0);

  while (1) {
    lora_uplink_msg_t msg = {0}; // Zero-initialize to prevent garbage data

    if (lora_get_event(&msg, K_FOREVER)) {

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
