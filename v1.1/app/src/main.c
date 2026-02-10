/*
 * FlexBox v1.2 — Main entry. Initializes hardware, starts LoRa + SMF + Input
 * threads; main then sleeps. Orchestration is via SMF (system mode FSM).
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "battery_adc.h"
#include "button_counter_store.h"
#include "button_thread.h"
#include "buttons.h"
#include "housekeeping.h"
#include "devnonce_store.h"
#include "eeprom_probe.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
#include "nfc_service.h"
#include "payload_gen.h"
#include "power_ctrl.h"
#include "rail_manager.h"
#include "rtc.h"
#include "smf_system_mode.h"
#include "sys_config.h"
#include "time_sync.h"

#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

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
  /* One-shot maintenance: erase DevNonce and reinitialize with random value,
   * then reboot. */
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

  /* Initialize battery ADC (AIN3) for heartbeat / battery status uplink. */
  ret = battery_adc_init();
  if (ret != 0) {
    LOG_WRN("battery_adc_init failed (%d); heartbeat battery status disabled",
            ret);
  }

  /* NFC service (PN5180 ISO15693) for Staff check-in/out/vote */
  ret = nfc_service_init();
  if (ret != 0) {
    LOG_WRN("nfc_service_init failed (%d); Staff NFC disabled", ret);
  }

  /* Initialize LoRaWAN stack */
  ret = lora_app_init();
  if (ret < 0) {
    LOG_ERR("Failed to initialize LoRa app: %d", ret);
    return 0;
  }

  /* Start the LoRa thread (no blocking join; orchestration via SMF in Phase 2+)
   */
  k_thread_start(lora_thread_id);
  LOG_INF("LoRa thread started");

  /* Start SMF thread (system mode FSM); it blocks on its input queue */
  k_thread_start(smf_thread_id);
  LOG_INF("SMF thread started");

  ret = buttons_init();
  if (ret != 0) {
    LOG_ERR("buttons_init failed: %d", ret);
    return 0;
  }

  /* Input thread: posts button events to SMF queue; SMF invokes app_logic */
  k_thread_start(button_uplink_thread_id);
  LOG_INF("Input thread started");

  /* Housekeeping: periodic RTC sync (and later link check, battery + heartbeat) */
  (void)housekeeping_init();
  k_thread_start(housekeeping_thread_id);
  LOG_INF("Housekeeping thread started");

  /* Enter idle: turn off 3.3V, 3.3A, 3.6V. 1.8V stays on for LoRa + buttons. */
  rail_manager_enter_idle();
  LOG_INF("Rails released (idle); 3.3V/3.3A/3.6V off until requested");

  /* Main sleeps; buttons wake via GPIO; Input -> SMF -> app_logic -> LED/uplink */
  while (1) {
    k_sleep(K_FOREVER);
  }
}
