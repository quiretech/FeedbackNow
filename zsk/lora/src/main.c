/*
 * LoRaWAN Application
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "lora_app.h"
#include "payload_gen.h"
#include "sys_config.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define SEND_INTERVAL K_SECONDS(LORA_SEND_INTERVAL_SECONDS)

int main(void) {
  int ret;

  LOG_INF("=== LoRaWAN Application Starting ===");

  /* Initialize payload generator (stable NFC UID, counters) */
  payload_gen_init();

  /* Initialize LoRaWAN stack */
  ret = lora_app_init();
  if (ret < 0) {
    LOG_ERR("Failed to initialize LoRa app: %d", ret);
    return 0;
  }

  /* Start the LoRa thread for join and message processing */
  k_thread_start(lora_thread_id);
  LOG_INF("LoRa thread started");

  /* Wait a bit for the join to complete before sending data */
  LOG_INF("Waiting for LoRa join to complete...");
  k_sleep(K_SECONDS(30)); // Give time for join process

  LOG_INF("Starting periodic data transmission every %d seconds",
          LORA_SEND_INTERVAL_SECONDS);

  /* Main loop - generate + queue payloads every interval */
  while (1) {
    lora_uplink_msg_t msg = {0};
    uint8_t fport = 0;
    uint8_t payload[PAYLOAD_LEN_BYTES] = {0};

    /* Generate next rotating payload */
    ret = payload_gen_next(payload, &fport);
    if (ret != 0) {
      LOG_ERR("Failed to generate payload: %d", ret);
      k_sleep(SEND_INTERVAL);
      continue;
    }

    /* Prepare uplink message */
    msg.port = fport;
    msg.confirmed = false;
    msg.len = PAYLOAD_LEN_BYTES;
    memcpy(msg.data, payload, PAYLOAD_LEN_BYTES);
    payload_hex_dump(payload, PAYLOAD_LEN_BYTES);
    payload_decode_log(payload, PAYLOAD_LEN_BYTES);

    /* Queue the message for sending */
    ret = lora_put_event(&msg, K_NO_WAIT);
    if (ret != 0) {
      LOG_ERR("Failed to queue LoRa message: %d", ret);
    } else {
      LOG_INF("Message queued successfully");
    }

    /* Wait before sending next message */
    k_sleep(SEND_INTERVAL);
  }

  return 0;
}
