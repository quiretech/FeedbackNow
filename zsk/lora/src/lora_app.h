#ifndef LORA_APP_H
#define LORA_APP_H

#include "sys_config.h"
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/sys/atomic.h>

// #define LORAWAN_DEV_EUI                                                        \
//   { 0x3a, 0x2b, 0x35, 0xf2, 0x09, 0x78, 0x6d, 0x1e }
// #define LORAWAN_JOIN_EUI                                                       \
//   { 0x15, 0x4e, 0x09, 0x81, 0x5d, 0xf3, 0x08, 0x2c }
// #define LORAWAN_APP_KEY                                                        \
//   {                                                                            \
//     0x89, 0x77, 0xe5, 0x9d, 0x13, 0x46, 0x35, 0x7d, 0x00, 0x8c, 0x32, 0x66,    \
//         0xd5, 0xee, 0xa6, 0x1b                                                 \
//   }

#define LORAWAN_DEV_EUI                                                        \
  { 0x51, 0xc8, 0xb1, 0x01, 0xc3, 0x4d, 0x97, 0xf9 }
#define LORAWAN_JOIN_EUI                                                       \
  { 0x21, 0xa4, 0x25, 0xe7, 0x9c, 0x3e, 0xf5, 0xbc }
#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0xda, 0x73, 0xfa, 0x76, 0x21, 0x67, 0x65, 0x66, 0xb1, 0xae, 0x4e, 0x34,    \
        0xa3, 0x16, 0x7d, 0x63                                                 \
  }

/* Alternative key sets (commented out)
 * // #define LORAWAN_DEV_EUI { ... }
 * // #define LORAWAN_JOIN_EUI { ... }
 * // #define LORAWAN_APP_KEY { ... }
 */

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

/**
 * @brief Check if device has successfully joined the LoRaWAN network
 * @return true if joined, false otherwise
 */
static inline bool lora_is_joined(void) {
  return atomic_get(&lora_joined_flag) != 0;
}

/**
 * @brief Wait for LoRaWAN join to complete
 * @param timeout Maximum time to wait
 * @return 0 on success, -EAGAIN on timeout
 */
int lora_wait_for_join(k_timeout_t timeout);

/* Message queue API */
bool lora_get_event(lora_uplink_msg_t *msg, k_timeout_t timeout);
int lora_put_event(const lora_uplink_msg_t *msg, k_timeout_t timeout);

#endif // LORA_APP_H
