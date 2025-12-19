#ifndef LORA_APP_H
#define LORA_APP_H

#include "sys_config.h"
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>

// // #define NVS_LORAWAN_KEYS

#define LORAWAN_DEV_EUI                                                        \
  { 0x3a, 0x2b, 0x35, 0xf2, 0x09, 0x78, 0x6d, 0x1e }
#define LORAWAN_JOIN_EUI                                                       \
  { 0x15, 0x4e, 0x09, 0x81, 0x5d, 0xf3, 0x08, 0x2c }
#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0x89, 0x77, 0xe5, 0x9d, 0x13, 0x46, 0x35, 0x7d, 0x00, 0x8c, 0x32, 0x66,    \
        0xd5, 0xee, 0xa6, 0x1b                                                 \
  }

// #define LORAWAN_DEV_EUI                                                        \
//   { 0x5f, 0x0a, 0xbf, 0x0c, 0xc4, 0x9f, 0xf2, 0x51 }
// #define LORAWAN_JOIN_EUI                                                       \
//   { 0xac, 0x9c, 0x9b, 0x1c, 0x13, 0x3c, 0x86, 0x81 }
// #define LORAWAN_APP_KEY                                                        \
//   {                                                                            \
//     0xf5, 0x66, 0xbd, 0xe4, 0xa5, 0xdf, 0x95, 0x88, 0xba, 0x3d, 0xa4, 0xe6,    \
//         0xd1, 0xc2, 0x43, 0x27                                                 \
//   }

// bulk 1

// #define LORAWAN_DEV_EUI                                                        \
//   { 0x2b, 0xfc, 0x1f, 0xce, 0x71, 0x8e, 0x9f, 0xc4 }

// #define LORAWAN_JOIN_EUI                                                       \
//   { 0xca, 0x83, 0xaf, 0x8f, 0xb6, 0xda, 0xc7, 0x32 }

// #define LORAWAN_APP_KEY                                                        \
//   {                                                                            \
//     0x23, 0xc5, 0x66, 0xff, 0xb5, 0xa4, 0x0a, 0x7f, 0xea, 0xdf, 0x0b, 0x8c,    \
//         0x97, 0x2b, 0x84, 0x5c                                                 \
//   }

// // bulk 2

// #define LORAWAN_DEV_EUI                                                        \
//   { 0xb5, 0xaa, 0x0d, 0x81, 0xc1, 0x7c, 0xa2, 0xf9 }

// #define LORAWAN_JOIN_EUI                                                       \
//   { 0x11, 0x4a, 0xf7, 0x7e, 0x1e, 0xa5, 0x01, 0x4f }

// #define LORAWAN_APP_KEY                                                        \
//   {                                                                            \
//     0x0c, 0xa4, 0xd6, 0x39, 0x2b, 0x5a, 0xf2, 0xb2, 0x84, 0x3e, 0x25, 0x79,    \
//         0x97, 0x2e, 0x8f, 0xac                                                 \
//   }

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
