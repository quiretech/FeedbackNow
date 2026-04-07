#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include "sys_config.h"
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

// System states
typedef enum {
  SYSTEM_STATE_INIT,       // System initializing
  SYSTEM_STATE_READY,      // Ready for user input
  SYSTEM_STATE_PROCESSING, // Processing button event
  SYSTEM_STATE_SENDING,    // Sending LoRa message
  SYSTEM_STATE_ERROR,      // Error state
  SYSTEM_STATE_SLEEP       // Sleep mode (for future)
} system_state_t;

// System events
typedef enum {
  EVENT_BUTTON_PRESSED,
  EVENT_BUTTON_RELEASED,
  EVENT_LORA_SENT,
  EVENT_LORA_FAILED,
  EVENT_LED_UPDATED,
  EVENT_ERROR_OCCURRED,
  EVENT_SYSTEM_READY,
  EVENT_NFC_TAG_DETECTED,
  EVENT_NFC_SCAN_TIMEOUT
} system_event_t;

// Button event data
typedef struct {
  uint8_t button_id;
  int64_t timestamp_ms;
  bool pressed;
} button_event_data_t;

// LoRa event data
typedef struct {
  uint8_t port;
  uint8_t len;
  bool confirmed;
  uint8_t data[11]; // Max payload size
} lora_event_data_t;

// LED event data
typedef struct {
  uint8_t led_id;
  bool state;
  uint32_t duration_ms; // For future use
} led_event_data_t;

// NFC event data
typedef struct {
  uint8_t uid[NFC_UID_LENGTH];
  int64_t timestamp_ms;
  bool detected;
} nfc_event_data_t;

// System event structure
typedef struct {
  system_event_t event_type;
  union {
    button_event_data_t button_data;
    lora_event_data_t lora_data;
    led_event_data_t led_data;
    nfc_event_data_t nfc_data;
  };
} system_event_msg_t;

// State manager functions
int state_manager_init(void);
int state_manager_send_event(const system_event_msg_t *event);
system_state_t state_manager_get_current_state(void);
int state_manager_wait_for_state(system_state_t target_state,
                                 k_timeout_t timeout);

// Event queue for state manager
extern struct k_msgq state_event_queue;

// State manager thread ID
extern const k_tid_t state_manager_thread_id;

#endif // STATE_MANAGER_H
