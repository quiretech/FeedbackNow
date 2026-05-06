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
#include "boot_info.h"
#include "button_counter_store.h"
#include "button_thread.h"
#include "buttons.h"
#include "devnonce_store.h"
#include "display_manager.h"
#include "eeprom_probe.h"
#include "housekeeping.h"
#include "join_state_store.h"
#include "last_cleaned_store.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
#include "nfc_service.h"
#include "payload_gen.h"
#include "power_ctrl.h"
#include "rtos_thread_affinity.h"
#include "rail_manager.h"
#include "rtc.h"
#include "smf_system_mode.h"
#include "sys_config.h"
#include "time_sync.h"
#include "tz_offset_store.h"

#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/**
 * Single consolidated init: dependency order is EEPROM probe, rail_manager,
 * then EEPROM-backed stores (counters, devnonce, join_state), then RTC and
 * display stores, then peripherals (battery ADC, NFC, display, LoRa).
 * Factory-reset one-shots (if enabled) reboot and do not return.
 * Returns 0 on success, negative on failure.
 */
static int system_init(void) {
  int ret;

  /* Payload builder mutex; counters come from EEPROM after
   * button_counter_store_init() below. */
  payload_gen_init();

  ret = led_manager_init();
  if (ret != 0) {
    LOG_WRN("LED manager init failed (%d); LED feedback may be disabled", ret);
  }

  eeprom_probe_log();

  ret = rail_manager_init();
  if (ret != 0) {
    LOG_ERR("rail_manager_init failed: %d", ret);
    return ret;
  }

  ret = button_counter_store_init();
  if (ret != 0) {
    LOG_ERR("Button counter store init failed (%d) - rebooting", ret);
    sys_reboot(SYS_REBOOT_COLD);
  }

  ret = devnonce_store_init();
  if (ret != 0) {
    LOG_ERR("DevNonce store init failed (%d) - rebooting", ret);
    sys_reboot(SYS_REBOOT_COLD);
  }

  (void)join_state_store_init();

#if EEPROM_DEVNONCE_FACTORY_RESET_ON_BOOT
  ret = devnonce_store_factory_reset();
  if (ret != 0) {
    LOG_ERR("DevNonce factory reset failed (%d) - rebooting", ret);
  }
  LOG_INF("DevNonce factory reset complete - rebooting");
  sys_reboot(SYS_REBOOT_COLD);
#endif

#if EEPROM_COUNTERS_FACTORY_RESET_ON_BOOT
  ret = button_counter_store_factory_reset();
  if (ret != 0) {
    LOG_ERR("Factory reset failed (%d) - rebooting", ret);
  }
  sys_reboot(SYS_REBOOT_COLD);
#endif

  ret = rtc_app_init();
  if (ret != 0) {
    LOG_WRN("RTC init not available (%d); button timestamps will use uptime",
            ret);
    /* Continue without RTC - system can still function with uptime fallback */
  }

  (void)last_cleaned_store_init();
  (void)tz_offset_store_init();
  uint32_t rtc_epoch = 0;
  if (rtc_get_epoch_seconds(&rtc_epoch) == 0) {
    (void)last_cleaned_store_ensure_non_empty(rtc_epoch);
  }

  ret = battery_adc_init();
  if (ret != 0) {
    LOG_WRN("battery_adc_init failed (%d); heartbeat battery status disabled",
            ret);
  }

  ret = nfc_service_init();
  if (ret != 0) {
    LOG_WRN("nfc_service_init failed (%d); Staff NFC disabled", ret);
  }

  (void)display_manager_init();

  ret = lora_app_init();
  if (ret < 0) {
    LOG_ERR("Failed to initialize LoRa app: %d", ret);
    return ret;
  }

  return 0;
}

int main(void) {
  int ret;

  LOG_SECTION_INF("FlexBox+ v" FW_VERSION_STRING " starting");

  /* Capture + clear reset cause before anything else so later code (Stage 3
   * install screen) can gate UI on commissioning-class boots only. Failure is
   * non-fatal; we just lose the gating signal. */
  (void)boot_info_init();

  k_sleep(K_SECONDS(1));

  ret = power_ctrl_init();
  if (ret < 0) {
    LOG_ERR("Power control init failed: %d", ret);
    return ret;
  }

  power_ctrl_set(POWER_EN_3V3, true);
  power_ctrl_set(POWER_EN_1V8, true);
  power_ctrl_set(POWER_EN_3V3A, true);
  rtc_notify_3v3a_enabled();
  power_ctrl_set(POWER_EN_3V6, true);

  k_sleep(K_SECONDS(1));

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
  k_msleep(50);
#endif

  ret = system_init();
  if (ret != 0) {
    return ret;
  }

  /* Enter idle before worker threads run to avoid refcount-reset races. */
  rail_manager_enter_idle();
  LOG_INF("rails idle (3.3 / 3.3A / 3.6 off until use)");

  /* Centralized thread start (single block for ordering and priorities). */
  k_thread_start(led_ui_thread_id);
  (void)led_manager_wait_until_ready(K_SECONDS(1));
  k_thread_start(nfc_worker_id);
  (void)nfc_service_wait_until_ready(K_SECONDS(1));
#if EPD_ENABLED
  /* Logo uses long EPD SPI; finish before LoRa thread joins (same SPI bus). */
  display_show_logo_sync();
#endif
  k_thread_start(lora_thread_id);
  (void)lora_wait_until_ready(K_SECONDS(1));
  k_thread_start(smf_thread_id);

  ret = buttons_init();
  if (ret != 0) {
    LOG_ERR("buttons_init failed: %d", ret);
    return 0;
  }

  k_thread_start(button_uplink_thread_id);
  (void)button_thread_wait_until_ready(K_SECONDS(1));

  (void)housekeeping_init();
  k_thread_start(housekeeping_thread_id);
  (void)housekeeping_wait_until_ready(K_SECONDS(1));

  LOG_INF("workers running: LED NFC LoRa SMF input HK");

  /* Signal SMF: all inits and threads started ("system go"). */
  if (smf_post_event(SMF_EVT_SYSTEM_READY, 0, k_uptime_get()) != 0) {
    LOG_WRN("Failed to post SYSTEM_READY to SMF");
  } else if (smf_wait_until_ready(K_SECONDS(2)) != 0) {
    LOG_WRN("SMF ready ack timeout; proceeding with rail idle transition");
  }

#if !EPD_ENABLED
  display_show_logo();
#endif

  /* Main sleeps; buttons wake via GPIO; Input -> SMF -> app_logic -> LED/uplink
   */
  for (;;) {
    k_sleep(K_FOREVER);
  }
}
