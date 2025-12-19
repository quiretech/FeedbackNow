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
#include <zephyr/sys/util.h>

#include "button_thread.h"
#include "buttons.h"
#include "lora_app.h"
#include "payload_gen.h"
#include "power_ctrl.h"
#include "rtc.h"
#include "sys_config.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define SEND_INTERVAL K_SECONDS(LORA_SEND_INTERVAL_SECONDS)

int main(void) {
  int ret;

  LOG_INF("=== LoRaWAN Application Starting ===");

  ret = power_ctrl_init();
  if (ret < 0) {
    LOG_ERR("Power control init failed: %d", ret);
    return ret;
  }

  power_ctrl_set(POWER_EN_3V3, true);
  power_ctrl_set(POWER_EN_1V8, true);
  power_ctrl_set(POWER_EN_3V3A, false);
  power_ctrl_set(POWER_EN_3V6, true);

  /* Initialize payload generator (stable NFC UID, counters) */
  payload_gen_init();

  /* Optional RTC init (button mode uses RTC timestamps) */
  ret = rtc_app_init();
  if (ret != 0) {
    LOG_WRN("RTC init not available (%d); button timestamps may fall back",
            ret);
  }

  /* Initialize LoRaWAN stack */
  ret = lora_app_init();
  if (ret < 0) {
    LOG_ERR("Failed to initialize LoRa app: %d", ret);
    return 0;
  }

  /* Start the LoRa thread for join and message processing */
  k_thread_start(lora_thread_id);
  LOG_INF("LoRa thread started");

  /* Wait for actual join completion (timeout after 2 minutes) */
  LOG_INF("Waiting for LoRa join to complete...");
  ret = lora_wait_for_join(K_SECONDS(120));
  if (ret != 0) {
    LOG_ERR("LoRa join did not complete within timeout!");
    /* Continue anyway - the thread will keep trying to join */
  } else {
    LOG_INF("LoRa join confirmed!");
  }

  if (DEMO_USE_REAL_BUTTON_UPLINK) {
    LOG_INF("Demo mode: REAL BUTTON uplinks");

    ret = buttons_init();
    if (ret != 0) {
      LOG_ERR("buttons_init failed: %d", ret);
      return 0;
    }

    k_thread_start(button_uplink_thread_id);
    LOG_INF("Button uplink thread started");

    while (1) {
      k_sleep(K_SECONDS(1));
    }
  }

  LOG_INF("Demo mode: PSEUDO simulation; sending every %d seconds",
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
    if (ret == -ENOTCONN) {
      LOG_WRN("Not joined to network yet, skipping message");
    } else if (ret != 0) {
      LOG_ERR("Failed to queue LoRa message: %d", ret);
    } else {
      LOG_INF("Message queued successfully");
    }

    /* Wait before sending next message */
    k_sleep(SEND_INTERVAL);
  }

  return 0;
}
