#include "buttons.h"
#include "k_config.h"
#include "leds.h"
#include "lora_app.h"
#include "nvs.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button_thread, LOG_LEVEL_INF);

extern struct k_msgq led_cmd_queue;
extern struct k_msgq lora_msgq;

void button_thread_func(void *, void *, void *);

static void button_payload_to_lora_msg(const button_payload_t *btn,
                                       lora_uplink_msg_t *msg) {
  msg->port = 1;
  msg->len = sizeof(btn->button_id); // The length is now exactly 1 byte

  // Copy the single-byte button ID into the data buffer
  msg->data[0] = btn->button_id;

  msg->confirmed = true;
}

void button_thread_func(void *a, void *b, void *c) {
  button_event_t btn_evt;
  led_cmd_t led_cmd;

  while (1) {
    LOG_INF("Button thread waiting for event...");
    buttons_get_event(&btn_evt, K_FOREVER);
    LOG_INF("Button %d %s at %lld", btn_evt.button_id,
            btn_evt.type == BUTTON_EVENT_PRESS ? "pressed" : "released",
            btn_evt.timestamp_ms);

    /* Simple logic: turn on LED with same ID when pressed, off when released */
    led_cmd.led_id = btn_evt.button_id;
    led_cmd.cmd = (btn_evt.type == BUTTON_EVENT_PRESS) ? LED_ON : LED_OFF;
    // Temporarily comment out LED control to test if it's causing the MPU fault
    // k_msgq_put(&led_cmd_queue, &led_cmd, K_NO_WAIT);

    // Create message on stack with proper initialization
    lora_uplink_msg_t lora_msg = {0}; // Zero-initialize the entire structure

    // Then set the fields properly
    lora_msg.port = 1;
    lora_msg.len = 1;
    lora_msg.data[0] = btn_evt.button_id; // Set the first byte of the array
    lora_msg.confirmed = false;           // Try unconfirmed messages

    if (lora_msg.len > LORA_PAYLOAD_MAX) {
      LOG_ERR("Payload too long for current datarate! (%d bytes)",
              lora_msg.len);
    } else {
      int ret = k_msgq_put(&lora_msgq, &lora_msg, K_NO_WAIT);
      if (ret == 0) {
        LOG_INF("LoRa message queued, len=%d", lora_msg.len);
        LOG_INF("Queue usage: %d / %d", k_msgq_num_used_get(&lora_msgq),
                LORA_MSGQ_SIZE);
      } else {
        LOG_ERR("Failed to queue LoRa message: %d", ret);
      }
    }
  }
}

K_THREAD_DEFINE(button_thread_id, BUTTON_THREAD_STACK_SIZE, button_thread_func,
                NULL, NULL, NULL, BUTTON_THREAD_PRIORITY, 0, 0);