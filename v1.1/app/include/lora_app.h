#ifndef LORA_APP_H
#define LORA_APP_H

#include "eui_keys.h"
#include "sys_config.h"
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/sys/atomic.h>

/**
 * @brief Initialize LoRaWAN stack
 */
int lora_app_init(void);

/**
 * @brief Downlink callback
 */
void lora_app_dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr,
                          uint8_t len, const uint8_t *hex_data);

/**
 * @brief Data rate change callback
 */
void lora_app_dr_changed(enum lorawan_datarate dr);

// LoRa configuration is now in sys_config.h

typedef struct {
  uint8_t port;
  uint8_t len;
  bool confirmed;
  uint8_t data[LORA_MAX_PAYLOAD_SIZE]; // Moved to end for better alignment
} lora_uplink_msg_t;

typedef struct {
  uint8_t button_id; // Unique button identifier
} button_payload_t;

extern struct k_msgq lora_msgq;
extern struct k_mutex lora_send_mutex;

// LoRa thread ID
extern const k_tid_t lora_thread_id;

/* Join status tracking - atomic flag for thread safety */
extern atomic_t lora_joined_flag;

/* Semaphore to signal join completion to waiting threads */
extern struct k_sem lora_join_sem;

/* First-boot: signal LoRa thread to start join (after Staff + 0+1+2) */
extern struct k_sem lora_join_trigger_sem;
void lora_request_join(void);

/**
 * @brief Check if device has successfully joined the LoRaWAN network
 * @return true if joined, false otherwise
 */
static inline bool lora_is_joined(void) {
  return atomic_get(&lora_joined_flag) != 0;
}

/* Message queue API */
bool lora_get_event(lora_uplink_msg_t *msg, k_timeout_t timeout);
int lora_put_event(const lora_uplink_msg_t *msg, k_timeout_t timeout);

#endif // LORA_APP_H
