#include "state_manager.h"
#include "led_manager.h"
#include "lora_manager.h"
#include "sys_config.h"
#include "system_monitor.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(state_manager, LOG_LEVEL_INF);

// State manager variables
static system_state_t current_state = SYSTEM_STATE_INIT;
static struct k_mutex state_mutex;
static struct k_sem state_change_sem;

// Event queue for state manager
K_MSGQ_DEFINE(state_event_queue, sizeof(system_event_msg_t),
              STATE_EVENT_QUEUE_SIZE, STATE_EVENT_QUEUE_ALIGNMENT);

// State transition table
static const char *state_names[] = {"INIT",    "READY", "PROCESSING",
                                    "SENDING", "ERROR", "SLEEP"};

static const char *event_names[] = {
    "BUTTON_PRESSED", "BUTTON_RELEASED",  "LORA_SENT",
    "LORA_FAILED",    "LED_UPDATED",      "ERROR_OCCURRED",
    "SYSTEM_READY",   "NFC_TAG_DETECTED", "NFC_SCAN_TIMEOUT"};

int state_manager_init(void) {
  k_mutex_init(&state_mutex);
  k_sem_init(&state_change_sem, 0, 1);

  current_state = SYSTEM_STATE_INIT;
  LOG_INF("State manager initialized");
  return 0;
}

int state_manager_send_event(const system_event_msg_t *event) {
  if (event == NULL) {
    LOG_ERR("NULL event pointer");
    return -EINVAL;
  }

  int ret = k_msgq_put(&state_event_queue, event, K_NO_WAIT);
  if (ret != 0) {
    LOG_ERR("Failed to queue event: %d", ret);
    return ret;
  }

  LOG_DBG("Event queued: %s", event_names[event->event_type]);
  return 0;
}

system_state_t state_manager_get_current_state(void) {
  k_mutex_lock(&state_mutex, K_FOREVER);
  system_state_t state = current_state;
  k_mutex_unlock(&state_mutex);
  return state;
}

int state_manager_wait_for_state(system_state_t target_state,
                                 k_timeout_t timeout) {
  return k_sem_take(&state_change_sem, timeout);
}

static void state_manager_process_event(const system_event_msg_t *event) {
  system_state_t new_state = current_state;

  switch (current_state) {
  case SYSTEM_STATE_INIT:
    if (event->event_type == EVENT_SYSTEM_READY) {
      new_state = SYSTEM_STATE_READY;
    }
    break;

  case SYSTEM_STATE_READY:
    if (event->event_type == EVENT_BUTTON_PRESSED) {
      new_state = SYSTEM_STATE_PROCESSING;
    }
    break;

  case SYSTEM_STATE_PROCESSING:
    if (event->event_type == EVENT_LED_UPDATED) {
      new_state = SYSTEM_STATE_SENDING;
    } else if (event->event_type == EVENT_ERROR_OCCURRED) {
      new_state = SYSTEM_STATE_ERROR;
    }
    break;

  case SYSTEM_STATE_SENDING:
    if (event->event_type == EVENT_LORA_SENT) {
      new_state = SYSTEM_STATE_READY;
    } else if (event->event_type == EVENT_LORA_FAILED) {
      new_state = SYSTEM_STATE_ERROR;
    }
    break;

  case SYSTEM_STATE_ERROR:
    if (event->event_type == EVENT_SYSTEM_READY) {
      new_state = SYSTEM_STATE_READY;
    }
    break;

  case SYSTEM_STATE_SLEEP:
    if (event->event_type == EVENT_BUTTON_PRESSED) {
      new_state = SYSTEM_STATE_READY;
    }
    break;
  }

  // Update state if changed
  if (new_state != current_state) {
    system_state_t old_state = current_state;
    k_mutex_lock(&state_mutex, K_FOREVER);
    current_state = new_state;
    k_mutex_unlock(&state_mutex);

    LOG_INF("State transition: %s -> %s", state_names[old_state],
            state_names[new_state]);

    k_sem_give(&state_change_sem);
  }
}

static void
state_manager_handle_button_event(const button_event_data_t *button_data) {
  LOG_INF("Button %d %s", button_data->button_id,
          button_data->pressed ? "pressed" : "released");

  if (button_data->pressed) {
    // Send LED command for visual feedback
    led_cmd_t led_cmd = {.led_id = button_data->button_id,
                         .cmd = LED_CMD_BLINK_ONCE,
                         .duration_ms = LED_BLINK_DURATION_MS};
    led_manager_send_command(&led_cmd);

    // Send LoRa message
    lora_manager_send_button_event(button_data->button_id);

    // Update metrics
    system_monitor_increment_counter("button_events_processed");
  }
}

static void
state_manager_handle_lora_event(const lora_event_data_t *lora_data) {
  LOG_INF("LoRa event: port=%d, len=%d", lora_data->port, lora_data->len);

  // Update metrics
  system_monitor_increment_counter("lora_messages_sent");
}

static void state_manager_handle_led_event(const led_event_data_t *led_data) {
  LOG_DBG("LED %d %s", led_data->led_id, led_data->state ? "ON" : "OFF");

  // Update metrics
  system_monitor_increment_counter("led_commands_processed");
}

void state_manager_thread(void *a, void *b, void *c) {
  system_event_msg_t event;

  LOG_INF("=== STATE MANAGER THREAD ENTRY ===");
  LOG_INF("State manager thread started - Thread ID: %p", k_current_get());
  LOG_INF("State manager thread priority: %d",
          k_thread_priority_get(k_current_get()));

  while (1) {
    LOG_DBG("State manager waiting for events...");
    if (k_msgq_get(&state_event_queue, &event, K_FOREVER) == 0) {
      LOG_INF("=== STATE MANAGER PROCESSING EVENT ===");
      LOG_DBG("Processing event: %s", event_names[event.event_type]);

      switch (event.event_type) {
      case EVENT_BUTTON_PRESSED:
      case EVENT_BUTTON_RELEASED:
        state_manager_handle_button_event(&event.button_data);
        break;

      case EVENT_LORA_SENT:
      case EVENT_LORA_FAILED:
        state_manager_handle_lora_event(&event.lora_data);
        break;

      case EVENT_LED_UPDATED:
        state_manager_handle_led_event(&event.led_data);
        break;

      case EVENT_ERROR_OCCURRED:
        LOG_ERR("System error occurred");
        system_monitor_increment_counter("system_errors");
        break;

      case EVENT_SYSTEM_READY:
        LOG_INF("System ready");
        break;

      case EVENT_NFC_TAG_DETECTED:
        LOG_INF(
            "NFC tag detected - UID: %02X %02X %02X %02X %02X %02X %02X %02X",
            event.nfc_data.uid[0], event.nfc_data.uid[1], event.nfc_data.uid[2],
            event.nfc_data.uid[3], event.nfc_data.uid[4], event.nfc_data.uid[5],
            event.nfc_data.uid[6], event.nfc_data.uid[7]);
        system_monitor_increment_counter("nfc_tags_detected");
        break;

      case EVENT_NFC_SCAN_TIMEOUT:
        LOG_WRN("NFC scan timeout - no tag detected");
        system_monitor_increment_counter("nfc_scan_timeouts");
        break;
      }

      // Process state transition
      state_manager_process_event(&event);
    }
  }
}

K_THREAD_DEFINE(state_manager_thread_id, STATE_MANAGER_THREAD_STACK_SIZE,
                state_manager_thread, NULL, NULL, NULL,
                STATE_MANAGER_THREAD_PRIORITY, 0, 0);
