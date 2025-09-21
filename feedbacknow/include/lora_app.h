#ifndef LORA_APP_H
#define LORA_APP_H

#include "sys_config.h"
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/lorawan/lorawan.h>

// // #define NVS_LORAWAN_KEYS

#define LORAWAN_DEV_EUI                                                        \
  { 0xDC, 0x17, 0x0E, 0x89, 0x42, 0x6C, 0xC7, 0x97 }
#define LORAWAN_JOIN_EUI                                                       \
  { 0xEB, 0x62, 0x2C, 0x56, 0xE2, 0xF2, 0xAD, 0xE8 }
#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0x1C, 0x57, 0xE2, 0x1C, 0xD4, 0xF5, 0xB2, 0x9A, 0x05, 0x8A, 0x6D, 0x12,    \
        0x44, 0x63, 0x7F, 0xFE                                                 \
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
