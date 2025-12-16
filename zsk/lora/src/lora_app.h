#ifndef LORA_APP_H
#define LORA_APP_H

#include "sys_config.h"
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>

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
  { 0xb9, 0xca, 0xf8, 0x73, 0x7a, 0x54, 0x8e, 0xcd }
#define LORAWAN_JOIN_EUI                                                       \
  { 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0xab, 0x51, 0x38, 0xaf, 0xc0, 0xd0, 0xda, 0xad, 0xb9, 0x6a, 0x90, 0xe8,    \
        0xcb, 0x6b, 0x13, 0x00                                                 \
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

/* Message queue API */
bool lora_get_event(lora_uplink_msg_t *msg, k_timeout_t timeout);
int lora_put_event(const lora_uplink_msg_t *msg, k_timeout_t timeout);

#endif // LORA_APP_H
