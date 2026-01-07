#include "lora_manager.h"
#include "lora_app.h"
#include "power_rail_mgr.h"
#include "state_manager.h"
#include "sys_config.h"
#include "system_monitor.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>

LOG_MODULE_REGISTER(lora_manager, LOG_LEVEL_INF);

// LoRa manager variables
static lora_status_t current_status = LORA_STATUS_IDLE;
static struct k_mutex status_mutex;
static uint32_t messages_sent = 0;
static uint32_t messages_failed = 0;
static uint32_t retry_count = 0;

// LoRa message queue
K_MSGQ_DEFINE(lora_message_queue, sizeof(lora_message_t),
              LORA_MESSAGE_QUEUE_SIZE, LORA_MESSAGE_ALIGNMENT);

// Retry work item
static struct k_work retry_work;
static struct k_timer retry_timer;

// Forward declarations
static void lora_retry_work_handler(struct k_work *work);
static void lora_retry_timer_handler(struct k_timer *timer);

int lora_manager_init(void) {
  k_mutex_init(&status_mutex);
  k_work_init(&retry_work, lora_retry_work_handler);
  k_timer_init(&retry_timer, lora_retry_timer_handler, NULL);

  current_status = LORA_STATUS_IDLE;
  LOG_INF("LoRa manager initialized");
  return 0;
}

int lora_manager_send_message(const lora_message_t *msg) {
  if (msg == NULL) {
    LOG_ERR("NULL message pointer");
    return -EINVAL;
  }

  if (msg->len == 0 || msg->len > LORA_MAX_PAYLOAD_SIZE) {
    LOG_ERR("Invalid message length: %d", msg->len);
    return -EINVAL;
  }

  int ret = k_msgq_put(&lora_message_queue, msg, K_NO_WAIT);
  if (ret != 0) {
    LOG_ERR("Failed to queue LoRa message: %d", ret);
    return ret;
  }

  LOG_DBG("LoRa message queued: port %d, len %d", msg->port, msg->len);
  return 0;
}

int lora_manager_send_button_event(uint8_t button_id) {
  lora_message_t msg = {0};

  // Create button event message
  msg.port = LORA_BUTTON_PORT;
  msg.len = 1;
  msg.data[0] = button_id;
  msg.confirmed = true;
  msg.retry_count = 0;
  msg.timestamp_ms = k_uptime_get();

  return lora_manager_send_message(&msg);
}

lora_status_t lora_manager_get_status(void) {
  k_mutex_lock(&status_mutex, K_FOREVER);
  lora_status_t status = current_status;
  k_mutex_unlock(&status_mutex);
  return status;
}

int lora_manager_get_queue_usage(void) {
  return k_msgq_num_used_get(&lora_message_queue);
}

static void lora_manager_set_status(lora_status_t status) {
  k_mutex_lock(&status_mutex, K_FOREVER);
  current_status = status;
  k_mutex_unlock(&status_mutex);
}

static int lora_manager_send_with_retry(lora_message_t *msg) {
  int ret;
  int attempts = 0;

  LOG_INF("Sending LoRa message: port %d, len %d, retry %d", msg->port,
          msg->len, msg->retry_count);

  lora_manager_set_status(LORA_STATUS_SENDING);

  while (attempts < LORA_MAX_RETRIES) {
    /* LoRa requires 3V3A OFF while the radio is active (TX + RX windows). */
    (void)power_rail_mgr_require_3v3a_off(POWER_RAIL_CLIENT_LORA, K_FOREVER);
    ret = lorawan_send(msg->port, msg->data, msg->len,
                       msg->confirmed ? LORAWAN_MSG_CONFIRMED
                                      : LORAWAN_MSG_UNCONFIRMED);
    k_sleep(K_MSEC(LORA_3V3A_GUARD_MS));
    power_rail_mgr_release_3v3a_off(POWER_RAIL_CLIENT_LORA);

    if (ret == 0) {
      // Success
      lora_manager_set_status(LORA_STATUS_SUCCESS);
      messages_sent++;
      LOG_INF("LoRa message sent successfully");

      // Notify state manager
      system_event_msg_t event = {.event_type = EVENT_LORA_SENT,
                                  .lora_data = {.port = msg->port,
                                                .len = msg->len,
                                                .confirmed = msg->confirmed}};
      state_manager_send_event(&event);

      return 0;
    } else if (ret == -EAGAIN) {
      LOG_WRN("LoRa send busy, retrying...");
      k_sleep(K_MSEC(LORA_SEND_BUSY_RETRY_MS));
      attempts++;
    } else {
      LOG_ERR("LoRa send failed: %d", ret);
      break;
    }
  }

  // All retries failed
  lora_manager_set_status(LORA_STATUS_FAILED);
  messages_failed++;
  retry_count += attempts;

  LOG_ERR("LoRa send failed after %d attempts", attempts);

  // Notify state manager
  system_event_msg_t event = {.event_type = EVENT_LORA_FAILED,
                              .lora_data = {.port = msg->port,
                                            .len = msg->len,
                                            .confirmed = msg->confirmed}};
  state_manager_send_event(&event);

  return ret;
}

static void lora_retry_work_handler(struct k_work *work) {
  LOG_INF("LoRa retry work handler");
  // This could be used for more complex retry logic in the future
}

static void lora_retry_timer_handler(struct k_timer *timer) {
  k_work_submit(&retry_work);
}

void lora_manager_thread(void *a, void *b, void *c) {
  lora_message_t msg;

  LOG_INF("=== LORA MANAGER THREAD ENTRY ===");
  LOG_INF("LoRa manager thread started - Thread ID: %p", k_current_get());
  LOG_INF("LoRa manager thread priority: %d",
          k_thread_priority_get(k_current_get()));

  while (1) {
    LOG_DBG("LoRa manager waiting for messages...");
    if (k_msgq_get(&lora_message_queue, &msg, K_FOREVER) == 0) {
      LOG_INF("=== LORA MANAGER PROCESSING MESSAGE ===");
      LOG_DBG("Processing LoRa message: port %d, len %d", msg.port, msg.len);

      int ret = lora_manager_send_with_retry(&msg);
      if (ret != 0) {
        LOG_ERR("Failed to send LoRa message: %d", ret);

        // Update metrics
        system_monitor_increment_counter("lora_messages_failed");
      } else {
        // Update metrics
        system_monitor_increment_counter("lora_messages_sent");
      }
    }
  }
}

K_THREAD_DEFINE(lora_manager_thread_id, LORA_THREAD_STACK_SIZE,
                lora_manager_thread, NULL, NULL, NULL, LORA_THREAD_PRIORITY, 0,
                0);
