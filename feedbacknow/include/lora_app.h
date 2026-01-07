#ifndef LORA_APP_H
#define LORA_APP_H

#include "sys_config.h"
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>

// // #define NVS_LORAWAN_KEYS

// # FlexBox1
// #define LORAWAN_DEV_EUI \
//   { 0x20, 0x25, 0x97, 0x19, 0x17, 0x7e, 0xae, 0xe8 }

// #define LORAWAN_JOIN_EUI                                                       \
//   { 0xa3, 0x95, 0xba, 0x60, 0x71, 0xbd, 0x03, 0x24 }

// #define LORAWAN_APP_KEY                                                        \
//   {                                                                            \
//     0x17, 0x17, 0x82, 0x72, 0xc7, 0xff, 0x39, 0x0d, 0x30, 0x81, 0xae, 0xd8,    \
//         0xfd, 0xf2, 0xcf, 0x6b                                                 \
//   }

// // # FlexBox2
#define LORAWAN_DEV_EUI                                                        \
  { 0xb2, 0x9e, 0xbb, 0x3f, 0x65, 0x94, 0xfa, 0x3f }

#define LORAWAN_JOIN_EUI                                                       \
  { 0xde, 0x12, 0xfe, 0x6d, 0x86, 0x43, 0x57, 0x0b }

#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0x01, 0xd9, 0x7d, 0x74, 0x57, 0x9c, 0xca, 0x66, 0x55, 0x94, 0x7f, 0x1c,    \
        0xee, 0x62, 0x09, 0x87                                                 \
  }

// Unit 36

// #define LORAWAN_DEV_EUI                                                        \
//   { 0x46, 0x4c, 0x58, 0x8a, 0x04, 0xbf, 0x57, 0x71 }

// #define LORAWAN_JOIN_EUI                                                       \
//   { 0x42, 0x03, 0xb1, 0xf7, 0xa0, 0x64, 0xfa, 0x64 }

// #define LORAWAN_APP_KEY                                                        \
//   {                                                                            \
//     0x6a, 0x94, 0xfa, 0xfd, 0x37, 0xb2, 0x0c, 0x62, 0xd6, 0xae, 0x56, 0x8f,    \
//         0x71, 0xce, 0xb2, 0xa6                                                 \
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
