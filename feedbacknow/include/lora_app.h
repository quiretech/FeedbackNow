#ifndef LORA_APP_H
#define LORA_APP_H

#include "k_config.h"
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>

// // #define NVS_LORAWAN_KEYS

#define LORAWAN_DEV_EUI                                                        \
  { 0x29, 0x94, 0x3f, 0x54, 0x54, 0x16, 0x2b, 0xea }
#define LORAWAN_JOIN_EUI                                                       \
  { 0x84, 0xca, 0xc0, 0x02, 0x05, 0x82, 0x7a, 0x1c }
#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0xf0, 0x83, 0x71, 0xf3, 0x5e, 0x66, 0x8d, 0x8a, 0x41, 0xdc, 0x25, 0xa3,    \
        0x32, 0x59, 0xd2, 0x36                                                 \
  }

#define MAX_LORA_PAYLOAD 64

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
#define LORA_MSGQ_SIZE 32
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
