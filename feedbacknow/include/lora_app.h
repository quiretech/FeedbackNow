#ifndef LORA_APP_H
#define LORA_APP_H

#include "k_config.h"
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>

// // #define NVS_LORAWAN_KEYS

#define LORAWAN_DEV_EUI                                                        \
  { 0x0c, 0xb9, 0x42, 0xb3, 0x52, 0x3a, 0x0e, 0xa9 }
#define LORAWAN_JOIN_EUI                                                       \
  { 0x84, 0xca, 0xc0, 0x02, 0x05, 0x82, 0x7a, 0x1c }
#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0xc4, 0xcc, 0x52, 0x8d, 0x28, 0x64, 0xd0, 0xd8, 0x18, 0xe3, 0x5f, 0xc6,    \
        0xa9, 0xf3, 0x53, 0xb7                                                 \
  }

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

#define LORA_MSG_DATA_MAX 1
#define LORA_MSGQ_SIZE 10
#define LORA_PAYLOAD_MAX 11

typedef struct {
  uint8_t port;
  uint8_t len;
  bool confirmed;
  uint8_t data[LORA_PAYLOAD_MAX]; // Moved to end for better alignment
} lora_uplink_msg_t;

typedef struct {
  uint8_t button_id; // Unique button identifier
} button_payload_t;

extern struct k_msgq lora_msgq;
extern struct k_mutex lora_send_mutex;

/* Message queue API */
bool lora_get_event(lora_uplink_msg_t *msg, k_timeout_t timeout);
int lora_put_event(const lora_uplink_msg_t *msg, k_timeout_t timeout);

#endif // LORA_APP_H
