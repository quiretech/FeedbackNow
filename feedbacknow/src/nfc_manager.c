#include "nfc_manager.h"
#include "led_manager.h"
#include "lora_manager.h"
#include "state_manager.h"
#include "sys_config.h"

#include "pn5180.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nfc_manager, LOG_LEVEL_DBG);

// NFC system states
enum nfc_system_state {
  NFC_SLEEP,      // System sleeping, waiting for button
  NFC_WAKING,     // Button pressed, initializing
  NFC_SCANNING,   // Actively scanning for tags
  NFC_PROCESSING, // Tag detected, processing
  NFC_SENDING     // Sending to LoRa
};

// NFC manager state
static enum nfc_system_state nfc_state = NFC_SLEEP;
static const struct device *nfc_dev;
static K_MUTEX_DEFINE(nfc_state_mutex);
static K_MUTEX_DEFINE(nfc_mutex);

// NFC event queue
K_MSGQ_DEFINE(nfc_event_queue, sizeof(nfc_event_t), NFC_QUEUE_SIZE,
              NFC_QUEUE_ALIGNMENT);

// State management functions
static void set_nfc_state(enum nfc_system_state new_state) {
  k_mutex_lock(&nfc_state_mutex, K_FOREVER);

  const char *state_names[] = {"SLEEP", "WAKING", "SCANNING", "PROCESSING",
                               "SENDING"};
  LOG_INF("NFC state: %s -> %s", state_names[nfc_state],
          state_names[new_state]);

  nfc_state = new_state;
  k_mutex_unlock(&nfc_state_mutex);
}

static enum nfc_system_state get_nfc_state(void) {
  enum nfc_system_state current_state;
  k_mutex_lock(&nfc_state_mutex, K_FOREVER);
  current_state = nfc_state;
  k_mutex_unlock(&nfc_state_mutex);
  return current_state;
}

// Power management functions
static void nfc_power_down(void) {
  LOG_INF("NFC powering down");
  k_mutex_lock(&nfc_mutex, K_FOREVER);
  LOG_DBG("Configuring PN5180 to ISO15693 (idle mode)");
  int ret =
      pn5180_configure(nfc_dev, PN5180_PROTOCOL_ISO15693); // Reset to idle
  if (ret != 0) {
    LOG_ERR("Failed to configure PN5180 to idle: %d", ret);
  }
  k_mutex_unlock(&nfc_mutex);
  set_nfc_state(NFC_SLEEP);
}

static void nfc_power_up(void) {
  LOG_INF("NFC powering up");
  k_mutex_lock(&nfc_mutex, K_FOREVER);
  LOG_DBG("Configuring PN5180 to ISO14443A (active mode)");
  int ret = pn5180_configure(nfc_dev, PN5180_PROTOCOL_ISO14443A);
  if (ret != 0) {
    LOG_ERR("Failed to configure PN5180 to active: %d", ret);
  }
  k_mutex_unlock(&nfc_mutex);
  set_nfc_state(NFC_SCANNING);
}

// Event-driven single scan function
static int nfc_perform_single_scan(void) {
  uint8_t uid[NFC_UID_LENGTH];
  int ret;

  LOG_INF("Performing single NFC scan");
  set_nfc_state(NFC_SCANNING);

  // Single scan attempt with timeout
  LOG_DBG("Calling pn5180_get_inventory()");
  k_mutex_lock(&nfc_mutex, K_FOREVER);
  ret = pn5180_get_inventory(nfc_dev, uid, NFC_UID_LENGTH);
  k_mutex_unlock(&nfc_mutex);
  LOG_DBG("pn5180_get_inventory() returned: %d", ret);

  if (ret == 0) {
    // Tag detected
    nfc_event_t event;
    event.type = NFC_EVENT_TAG_DETECTED;
    memcpy(event.uid, uid, NFC_UID_LENGTH);
    event.timestamp_ms = k_uptime_get();

    if (k_msgq_put(&nfc_event_queue, &event, K_NO_WAIT) != 0) {
      LOG_WRN("NFC queue full, dropping tag detected event");
    }

    LOG_INF("Tag detected: %02X %02X %02X %02X %02X %02X %02X %02X", uid[0],
            uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);

    set_nfc_state(NFC_PROCESSING);
    return 0;
  } else {
    // No tag detected
    LOG_INF("No tag detected (ret=%d)", ret);
    set_nfc_state(NFC_SLEEP);
    return -ENODATA;
  }
}

// NFC manager thread
void nfc_manager_thread(void *a, void *b, void *c) {
  nfc_event_t event;
  system_event_msg_t system_event;

  LOG_INF("NFC manager thread started");

  while (1) {
    // Wait for NFC events
    if (k_msgq_get(&nfc_event_queue, &event, K_FOREVER) == 0) {
      LOG_DBG("Processing NFC event: %d", event.type);

      switch (event.type) {
      case NFC_EVENT_SCAN_START:
        nfc_manager_start_scan();
        break;

      case NFC_EVENT_TAG_DETECTED:
        LOG_INF("NFC tag detected: %02X %02X %02X %02X %02X %02X %02X %02X",
                event.uid[0], event.uid[1], event.uid[2], event.uid[3],
                event.uid[4], event.uid[5], event.uid[6], event.uid[7]);

        // Light up LED 3
        led_manager_set_led(3, true);

        // Send system event
        system_event.event_type = EVENT_NFC_TAG_DETECTED;
        memcpy(system_event.nfc_data.uid, event.uid, NFC_UID_LENGTH);
        system_event.nfc_data.timestamp_ms = event.timestamp_ms;
        system_event.nfc_data.detected = true;
        state_manager_send_event(&system_event);

        // Send LoRa message (reduced payload to fit 11-byte limit)
        lora_message_t lora_msg;
        lora_msg.port = LORA_NFC_PORT;
        lora_msg.len = NFC_UID_LENGTH + 3; // UID (8) + timestamp (3) = 11 bytes
        lora_msg.confirmed = false;

        // Pack UID and 24-bit timestamp
        memcpy(lora_msg.data, event.uid, NFC_UID_LENGTH);
        uint32_t timestamp_24 =
            (uint32_t)event.timestamp_ms & 0xFFFFFF; // 24-bit timestamp
        lora_msg.data[NFC_UID_LENGTH] = (timestamp_24 >> 16) & 0xFF;
        lora_msg.data[NFC_UID_LENGTH + 1] = (timestamp_24 >> 8) & 0xFF;
        lora_msg.data[NFC_UID_LENGTH + 2] = timestamp_24 & 0xFF;

        lora_manager_send_message(&lora_msg);

        // Return to sleep after processing
        nfc_power_down();
        break;

      case NFC_EVENT_SCAN_TIMEOUT:
        LOG_WRN("NFC scan timeout - no tag detected");

        // Send system event
        system_event.event_type = EVENT_NFC_SCAN_TIMEOUT;
        system_event.nfc_data.detected = false;
        system_event.nfc_data.timestamp_ms = event.timestamp_ms;
        state_manager_send_event(&system_event);

        // Return to sleep after timeout
        nfc_power_down();
        break;

      case NFC_EVENT_SCAN_STOP:
        nfc_manager_stop_scan();
        break;
      }
    }
  }
}

int nfc_manager_init(void) {
  // Get NFC device
  nfc_dev = DEVICE_DT_GET(DT_NODELABEL(pn5180));
  if (!device_is_ready(nfc_dev)) {
    LOG_ERR("NFC device not ready");
    return -ENODEV;
  }

  // Initialize NFC driver
  int ret = pn5180_init(nfc_dev);
  if (ret != 0) {
    LOG_ERR("Failed to initialize NFC driver: %d", ret);
    return ret;
  }

  // Configure for ISO14443A (most common NFC protocol)
  ret = pn5180_configure(nfc_dev, PN5180_PROTOCOL_ISO14443A);
  if (ret != 0) {
    LOG_ERR("Failed to configure NFC protocol: %d", ret);
    return ret;
  }

  LOG_INF("NFC manager initialized successfully");
  return 0;
}

int nfc_manager_start_scan(void) {
  if (get_nfc_state() != NFC_SLEEP) {
    LOG_WRN("NFC system not in sleep state");
    return -EBUSY;
  }

  LOG_INF("Starting NFC scan");
  set_nfc_state(NFC_WAKING);

  // Power up NFC system
  nfc_power_up();

  // Perform single scan
  return nfc_perform_single_scan();
}

int nfc_manager_stop_scan(void) {
  LOG_INF("Stopping NFC scan");
  nfc_power_down();
  return 0;
}

bool nfc_manager_is_scanning(void) { return get_nfc_state() != NFC_SLEEP; }

// Function to trigger NFC scan (called by button 3)
int nfc_manager_trigger_scan(void) {
  LOG_INF("Button triggered NFC scan");
  return nfc_manager_start_scan();
}
extern const k_tid_t nfc_manager_thread_id;

// NFC manager thread definition
K_THREAD_DEFINE(nfc_manager_thread_id, NFC_THREAD_STACK_SIZE,
                nfc_manager_thread, NULL, NULL, NULL, NFC_THREAD_PRIORITY, 0,
                -1);
