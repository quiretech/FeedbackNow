#include "button_thread.h"

#include "buttons.h"
#include "led_manager.h"
#include "lora_app.h"
#include "log_fmt.h"
#include "payload_gen.h"
#include "rtc.h"
#include "sys_config.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button_uplink, CONFIG_LOG_DEFAULT_LEVEL);

/* Map driver button index (0..NUM_BUTTONS-1) -> payload button_id */
static const uint8_t button_id_map[NUM_BUTTONS] = {0, 1, 2, 3, 4, 5, 6};

static void button_uplink_thread_fn(void *a, void *b, void *c) {
  button_event_t btn_evt;
  uint32_t last_press_ms[NUM_BUTTONS] = {0};
  uint32_t last_accepted_any_press_ms = 0;

  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  LOG_SECTION_INF("BUTTON UPLINK THREAD ENTRY");

  while (1) {
    (void)buttons_get_event(&btn_evt, K_FOREVER);

    if (btn_evt.button_id >= NUM_BUTTONS) {
      continue;
    }

    if (btn_evt.type != BUTTON_EVENT_PRESS) {
      continue; /* demo cares only about presses */
    }

    /* Simple debounce in thread context */
    uint32_t now_ms = (uint32_t)k_uptime_get_32();
    if ((now_ms - last_press_ms[btn_evt.button_id]) < BUTTON_DEBOUNCE_MS) {
      continue;
    }
    last_press_ms[btn_evt.button_id] = now_ms;

    /* Global anti-spam cooldown: ignore all presses for a while after any press */
    if ((now_ms - last_accepted_any_press_ms) < BUTTON_COOLDOWN_MS) {
      LOG_DBG("Button press ignored due to cooldown (%u ms remaining)",
              (uint32_t)(BUTTON_COOLDOWN_MS -
                         (now_ms - last_accepted_any_press_ms)));
      continue;
    }

    /* Blink LED for 1 second on press */
    (void)led_manager_blink_once(0);

    uint32_t epoch_s = 0;
    int ret = rtc_get_epoch_seconds(&epoch_s);
    if (ret != 0) {
      /* Fallback: still send something monotonic if RTC isn't available */
      epoch_s = (uint32_t)(k_uptime_get() / 1000U);
      LOG_WRN("RTC read failed (%d); using uptime seconds=%u", ret, epoch_s);
    }

    uint8_t payload[PAYLOAD_LEN_BYTES] = {0};
    uint8_t payload_button_id = button_id_map[btn_evt.button_id];

    uint32_t new_counter = 0;
    ret = payload_gen_build_button(payload_button_id, epoch_s, payload,
                                   &new_counter);
    if (ret != 0) {
      LOG_ERR("Failed to build button payload: %d", ret);
      continue;
    }

    lora_uplink_msg_t msg = {0};
    msg.port = FPORT_BUTTON;
    msg.confirmed = false;
    msg.len = PAYLOAD_LEN_BYTES;
    memcpy(msg.data, payload, PAYLOAD_LEN_BYTES);

    ret = lora_put_event(&msg, K_NO_WAIT);
    if (ret == -ENOTCONN) {
      LOG_WRN("Not joined yet; dropped button press id=%u", payload_button_id);
    } else if (ret != 0) {
      LOG_ERR("Failed to queue button uplink: %d", ret);
    } else {
      last_accepted_any_press_ms = now_ms;
      LOG_INF("Queued button uplink: btn=%u ctr=%u ts=%u", payload_button_id,
              new_counter, epoch_s);
    }
  }
}

/* Don't autostart; main.c starts it only in button demo mode */
K_THREAD_DEFINE(button_uplink_thread_id, BUTTON_THREAD_STACK_SIZE,
                button_uplink_thread_fn, NULL, NULL, NULL,
                BUTTON_THREAD_PRIORITY, 0, -1);