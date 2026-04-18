/**
 * Application logic: public vote (Normal mode single button).
 */
#include "app_logic.h"
#include "display_manager.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
#include "payload_gen.h"
#include "rtc.h"
#include "sys_config.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_logic, CONFIG_LOG_DEFAULT_LEVEL);

/* Map driver button index -> payload button_id (0..6 for backend) */
static const uint8_t button_id_map[] = {0, 1, 2, 3, 4, 5};

static uint32_t last_accepted_any_press_ms;

void app_logic_public_vote(uint8_t button_id) {
  uint32_t now_ms = (uint32_t)k_uptime_get_32();

  if (button_id >= NUM_BUTTONS) {
    LOG_WRN("[app_logic] Invalid button_id=%u (>= NUM_BUTTONS=%u)", button_id,
            NUM_BUTTONS);
    return;
  }

#if EPD_ENABLED
  /* No new votes while THANKS is showing until LAST_CLEANED render completes. */
  if (display_is_public_vote_ui_busy()) {
    LOG_DBG("[app_logic] public_vote ignored (vote UI busy)");
    return;
  }
#endif

  LOG_INF("[app_logic] public_vote called: button_id=%u, now=%u", button_id,
          now_ms);

  /* Public lockout: 5s after any accepted press (per FRD) */
  if ((now_ms - last_accepted_any_press_ms) < BUTTON_COOLDOWN_MS) {
    LOG_INF(
        "[app_logic] Vote BLOCKED by cooldown (%u ms left)",
        (uint32_t)(BUTTON_COOLDOWN_MS - (now_ms - last_accepted_any_press_ms)));
    return;
  }

  (void)led_manager_show(0, LED_PATTERN_BUTTON_ACCEPTED);
  /* EPD first: finish THANKS render (SPI), then LoRa/EEPROM with clear RX. */
  display_show_thanks_sync();

  uint32_t epoch_s = 0;
  int ret = rtc_get_epoch_seconds(&epoch_s);
  if (ret != 0) {
    epoch_s = (uint32_t)(k_uptime_get() / 1000U);
    LOG_WRN("RTC read failed (%d); using uptime s=%u", ret, epoch_s);
  }

  uint8_t payload_button_id = button_id_map[button_id];
  uint8_t payload[PAYLOAD_LEN_BYTES] = {0};
  uint32_t new_counter = 0;

  ret = payload_gen_build_button(payload_button_id, epoch_s, payload,
                                 &new_counter);
  if (ret != 0) {
    LOG_ERR("Build button payload failed: %d", ret);
    return;
  }

  lora_uplink_msg_t msg = {0};
  msg.port = FPORT_BUTTON;
  msg.confirmed = LORA_BUTTON_UPLINK_CONFIRMED;
  msg.len = PAYLOAD_LEN_BYTES;
  memcpy(msg.data, payload, PAYLOAD_LEN_BYTES);

  /* Local UX + counter already done; only queue uplink when joined. */
  if (lora_is_joined()) {
    ret = lora_put_event(&msg, K_NO_WAIT);
    if (ret != 0) {
      LOG_ERR("Queue button uplink failed: %d", ret);
    } else {
      last_accepted_any_press_ms = now_ms;
      LOG_INF("Queued button uplink: btn=%u ctr=%u ts=%u", payload_button_id,
              new_counter, epoch_s);
    }
  } else {
    last_accepted_any_press_ms =
        now_ms; /* Cooldown same as when joined (no EPD flood). */
    LOG_INF("[app_logic] Not joined; uplink skipped (btn=%u ctr=%u)",
            payload_button_id, new_counter);
  }
}
