/*
 * LoRaWAN Application
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "button_counter_store.h"
#include "button_thread.h"
#include "buttons.h"
#include "devnonce_store.h"
#include "eeprom_probe.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
#include "payload_gen.h"
#include "power_ctrl.h"
#include "rtc.h"
#include "smf_system_mode.h"
#include "sys_config.h"
#include "time_sync.h"

#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define SEND_INTERVAL K_SECONDS(LORA_SEND_INTERVAL_SECONDS)

int main(void) {
  int ret;

  LOG_SECTION_INF("LoRaWAN Application Starting");

  k_sleep(K_SECONDS(1));

  ret = power_ctrl_init();
  if (ret < 0) {
    LOG_ERR("Power control init failed: %d", ret);
    return ret;
  }

  /* Power up all domains for initialization */
  power_ctrl_set(POWER_EN_3V3, true);
  power_ctrl_set(POWER_EN_1V8, true);
  power_ctrl_set(POWER_EN_3V3A, true);
  power_ctrl_set(
      POWER_EN_3V6,
      true); /* LoRa radio - will be managed by lora_thread after join */

  k_sleep(K_SECONDS(1));

  /* Wait for I2C bus to stabilize after power-on */
#if DT_NODE_EXISTS(DT_NODELABEL(arduino_i2c))
  const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(arduino_i2c));
  if (i2c_dev != NULL) {
    int i2c_retries = 10;
    while (i2c_retries > 0 && !device_is_ready(i2c_dev)) {
      LOG_DBG("Waiting for I2C bus to be ready...");
      k_msleep(100);
      i2c_retries--;
    }
    if (device_is_ready(i2c_dev)) {
      LOG_INF("I2C bus ready");
    } else {
      LOG_WRN("I2C bus not ready after waiting");
    }
  }
  /* Additional delay for EEPROM power-on stabilization */
  k_msleep(50);
#endif

  /* Initialize payload generator (stable NFC UID, counters) */
  payload_gen_init();

  /* Initialize LED manager early (used for join + button feedback) */
  ret = led_manager_init();
  if (ret != 0) {
    LOG_WRN("LED manager init failed (%d); LED feedback may be disabled", ret);
  }

  /* EEPROM bring-up (readiness check only) */
  eeprom_probe_log();

  /* Persistent button counters (EEPROM is mandatory) */
  ret = button_counter_store_init();
  if (ret != 0) {
    LOG_ERR("Button counter store init failed (%d) - rebooting", ret);
    sys_reboot(SYS_REBOOT_COLD);
  }

  /* Persistent DevNonce (EEPROM-backed). Mandatory for robust OTAA joins. */
  ret = devnonce_store_init();
  if (ret != 0) {
    LOG_ERR("DevNonce store init failed (%d) - rebooting", ret);
    sys_reboot(SYS_REBOOT_COLD);
  }

#if EEPROM_DEVNONCE_FACTORY_RESET_ON_BOOT
  /* One-shot maintenance: erase DevNonce and reinitialize with random value, then reboot. */
  ret = devnonce_store_factory_reset();
  if (ret != 0) {
    LOG_ERR("DevNonce factory reset failed (%d) - rebooting", ret);
  }
  LOG_INF("DevNonce factory reset complete - rebooting");
  sys_reboot(SYS_REBOOT_COLD);
#endif

#if EEPROM_COUNTERS_FACTORY_RESET_ON_BOOT
  /* One-shot maintenance: wipe persistent counters, then reboot. */
  ret = button_counter_store_factory_reset();
  if (ret != 0) {
    LOG_ERR("Factory reset failed (%d) - rebooting", ret);
  }
  sys_reboot(SYS_REBOOT_COLD);
#endif

  /* Optional RTC init (button mode uses RTC timestamps) */
  ret = rtc_app_init();
  if (ret != 0) {
    LOG_WRN("RTC init not available (%d); button timestamps may fall back",
            ret);
    sys_reboot(SYS_REBOOT_COLD);
  }

  /* Initialize LoRaWAN stack */
  ret = lora_app_init();
  if (ret < 0) {
    LOG_ERR("Failed to initialize LoRa app: %d", ret);
    return 0;
  }

  /* Start the LoRa thread (no blocking join; orchestration via SMF in Phase 2+) */
  k_thread_start(lora_thread_id);
  LOG_INF("LoRa thread started");

  /* Start SMF thread (system mode FSM); it blocks on its input queue */
  k_thread_start(smf_thread_id);
  LOG_INF("SMF thread started");

  if (DEMO_USE_REAL_BUTTON_UPLINK) {
    LOG_INF("Demo mode: REAL BUTTON uplinks (Input -> SMF -> app logic)");

    ret = buttons_init();
    if (ret != 0) {
      LOG_ERR("buttons_init failed: %d", ret);
      return 0;
    }

    /* Input thread: posts button events to SMF queue; SMF invokes app_logic */
    k_thread_start(button_uplink_thread_id);
    LOG_INF("Input thread started");

    /* Main sleeps; buttons wake via GPIO; Input -> SMF -> app_logic -> LED/uplink */
    LOG_INF("Main thread sleeping (orchestration via SMF)");
    while (1) {
      k_sleep(K_FOREVER);
    }
  }

  LOG_INF("Demo mode: PSEUDO simulation; sending every %d seconds",
          LORA_SEND_INTERVAL_SECONDS);

  /* Main loop - generate + queue payloads every interval.
   * With tickless kernel enabled, k_sleep() allows CPU to enter deep sleep
   * during the 15-minute interval, providing maximum power savings.
   */
  while (1) {
    lora_uplink_msg_t msg = {0};
    uint8_t fport = 0;
    uint8_t payload[PAYLOAD_LEN_BYTES] = {0};

    /* Generate next rotating payload */
    ret = payload_gen_next(payload, &fport);
    if (ret != 0) {
      LOG_ERR("Failed to generate payload: %d", ret);
      /* Deep sleep during interval - tickless kernel handles this */
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

    /* Deep sleep during interval - tickless kernel allows CPU to enter
     * System ON sleep mode, waking only when the timer expires.
     * This provides maximum power savings between transmissions.
     */
    k_sleep(SEND_INTERVAL);
  }

  return 0;
}
