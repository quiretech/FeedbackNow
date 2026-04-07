#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

// LoRa message structure
typedef struct {
  uint8_t port;
  uint8_t len;
  bool confirmed;
  uint8_t data[11]; // Max payload size
  uint8_t retry_count;
  int64_t timestamp_ms;
} lora_message_t;

// LoRa manager status
typedef enum {
  LORA_STATUS_IDLE,
  LORA_STATUS_SENDING,
  LORA_STATUS_SUCCESS,
  LORA_STATUS_FAILED,
  LORA_STATUS_RETRY
} lora_status_t;

// LoRa manager functions
int lora_manager_init(void);
int lora_manager_send_message(const lora_message_t *msg);
int lora_manager_send_button_event(uint8_t button_id);
lora_status_t lora_manager_get_status(void);
int lora_manager_get_queue_usage(void);

// LoRa message queue
extern struct k_msgq lora_message_queue;

// LoRa manager thread function
void lora_manager_thread(void *a, void *b, void *c);

// Configuration is now in sys_config.h

#endif // LORA_MANAGER_H
