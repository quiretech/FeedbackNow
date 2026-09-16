/**
 * Application logic: public vote (Normal mode single button).
 */
#include "app_logic.h"
#include "log_fmt.h"
#include "display_manager.h"
#include "led_manager.h"
#include "log_fmt.h"
#include "lora_app.h"
#include "payload_gen.h"
#include "rtc.h"
#include "sys_config.h"

#if DEVICE_HW_VARIANT == FLEXBOX_PLUS_MED
#include "last_cleaned_store.h" // nk_co1
#endif /* end FLEXBOX_PLUS_MED */

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
    LOG_WRN("Invalid button_id=%u (>= NUM_BUTTONS=%u)", button_id,
            NUM_BUTTONS);
    return;
  }

#if EPD_ENABLED
  /* No new votes while THANKS is showing until LAST_CLEANED render completes. */
  if (display_is_public_vote_ui_busy()) {
    LOG_INF("public_vote ignored (vote UI busy)");
    return;
  }
#endif

  LOG_STATE("vote btn=%u", button_id);

  /* Public lockout: 5s after any accepted press (per FRD) */
  if ((now_ms - last_accepted_any_press_ms) < BUTTON_COOLDOWN_MS) {
    //LOG_STATE("App logic Mode ");
      LOG_STATE("vote blocked cooldown %ums",
              (uint32_t)(BUTTON_COOLDOWN_MS -
                         (now_ms - last_accepted_any_press_ms)));
    return;
  }

#if DEVICE_HW_VARIANT == FLEXBOX_PLUS_MED
  /* Logic for always forward pressing the buttons. 
     Next button pressed will always be one higher than the last */
  static uint8_t last_button_pressed = 1;
  if(button_id >= last_button_pressed - 1){
    last_button_pressed = button_id + 2;
    if(button_id >= 5){
      last_button_pressed = 1;
    }
  }
  else{
    return;
  }

#endif /* end FLEXBOX_PLUS_MED */



  LOG_STATE("led_manager_show ");
  (void)led_manager_show(0, LED_PATTERN_BUTTON_ACCEPTED);
  /* EPD first: finish THANKS render (SPI), then LoRa/EEPROM with clear RX. */

#if DEVICE_HW_VARIANT != FLEXBOX_PLUS_MED //nk_co1
  display_show_thanks_sync();
#endif

  uint32_t epoch_s = 0;
  int ret = rtc_get_epoch_seconds(&epoch_s);
  if (ret != 0) {
    epoch_s = (uint32_t)(k_uptime_get() / 1000U);
    LOG_DBG("RTC read failed (%d); using uptime s=%u", ret, epoch_s);
  }
  
#if DEVICE_HW_VARIANT == FLEXBOX_PLUS_MED
  room_alert_state_update(button_id, epoch_s); //update the room alert state with the new button pressed and the epoch timestamp
  rail_manager_request_3v3a();
  //(void)last_cleaned_store_set(epoch_s); // we are pass the epoch directly to the display function
#if EPD_ENABLED
  //epoch_s = 1778665556; // hardcoded epoch for testing
  rtc_get_epoch_seconds(&epoch_s);
  display_show_room_alert_status_sync();
#endif
  rail_manager_release_3v3a();

#endif /*end FLEXBOX_PLUS_MED*/

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

  if (lora_is_joined()) {
    ret = lora_put_event(&msg, K_NO_WAIT);
    if (ret != 0) {
      LOG_ERR("Queue button uplink failed: %d", ret);
    } else {
      last_accepted_any_press_ms = now_ms;
      LOG_EVT("vote UL btn=%u ctr=%u ts=%u", payload_button_id, new_counter,
              epoch_s);
    }
  } else {
    last_accepted_any_press_ms =
        now_ms; /* Cooldown same as when joined (no EPD flood). */
    LOG_INF("vote skip (not joined) btn=%u ctr=%u", payload_button_id,
            new_counter);
  }
}
