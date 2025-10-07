#include "buttons.h"
#include "nfc_manager.h"
#include "state_manager.h"
#include "sys_config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button_thread, LOG_LEVEL_INF);

void button_thread_func(void *, void *, void *);

void button_thread_func(void *a, void *b, void *c) {
  button_event_t btn_evt;
  system_event_msg_t event;

  LOG_INF("=== BUTTON THREAD ENTRY ===");
  LOG_INF("Button thread started - Thread ID: %p", k_current_get());
  LOG_INF("Button thread priority: %d", k_thread_priority_get(k_current_get()));

  while (1) {
    LOG_DBG("Button thread waiting for event...");
    buttons_get_event(&btn_evt, K_FOREVER);

    LOG_INF("Button %d %s at %lld", btn_evt.button_id,
            btn_evt.type == BUTTON_EVENT_PRESS ? "pressed" : "released",
            btn_evt.timestamp_ms);

    // Create system event
    event.event_type = (btn_evt.type == BUTTON_EVENT_PRESS)
                           ? EVENT_BUTTON_PRESSED
                           : EVENT_BUTTON_RELEASED;
    event.button_data.button_id = btn_evt.button_id;
    event.button_data.timestamp_ms = btn_evt.timestamp_ms;
    event.button_data.pressed = (btn_evt.type == BUTTON_EVENT_PRESS);

    // Send event to state manager
    int ret = state_manager_send_event(&event);
    if (ret != 0) {
      LOG_ERR("Failed to send button event to state manager: %d", ret);
    }

    // Special handling for button 0 (NFC trigger)
    if (btn_evt.button_id == 0 && btn_evt.type == BUTTON_EVENT_PRESS) {
      LOG_INF("Button 0 pressed - triggering NFC scan");
      nfc_manager_trigger_scan();
    }
  }
}

K_THREAD_DEFINE(button_thread_id, BUTTON_THREAD_STACK_SIZE, button_thread_func,
                NULL, NULL, NULL, BUTTON_THREAD_PRIORITY, 0, 0);