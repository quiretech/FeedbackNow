#ifndef PAYLOAD_GEN_H
#define PAYLOAD_GEN_H

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

/* Fixed payload size for DR0 US915 */
#define PAYLOAD_LEN_BYTES 11

/* Event types (FRD 4.5) */
enum payload_event_type {
  /* User actions */
  EVT_BUTTON = 0x00,   // ts + button_id(1) + counter(3)
  EVT_NFC_IN = 0x01,   // ts + uid(4)
  EVT_NFC_OUT = 0x02,  // ts + uid(4)
  EVT_NFC_VOTE = 0x03, // ts + button_id(2) + card_data(4)

  /* System / housekeeping */
  EVT_BATTERY_STATUS = 0x10, // ts + battery_mv(2) + percent(1) + flags(1)
  EVT_LOW_BATTERY = 0x11,    // ts + battery_mv(2) [+ optional threshold]
  EVT_COUNTER_SYNC = 0x12,   // ts + button_id(1) + counter(3)

  /* Reserved */
  EVT_FUTURE = 0xFF,
};
/* LoRaWAN FPorts (semantic routing) */
#define FPORT_BUTTON 10
#define FPORT_NFC 11
#define FPORT_HOUSEKEEPING 20
#define FPORT_ALARMS 30
#define FPORT_FUTURE 13

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize payload generator (mutex, etc.).
 */
void payload_gen_init(void);

/* Debug helpers (optional, for bring-up or tests) */
void payload_hex_dump(const uint8_t *buf, size_t len);
void payload_decode_log(const uint8_t *buf, size_t len);

/**
 * @brief Build a button payload for a specific button press.
 *
 * Payload format (11 bytes):
 *  - [0..3] timestamp (epoch seconds, big-endian)
 *  - [4]    event type (EVT_BUTTON)
 *  - [5]    button id
 *  - [6..8] per-button counter (24-bit, big-endian)
 *  - [9..10] reserved
 *
 * @param button_id    Button ID to encode (0..5 per FRD).
 * @param epoch_s      Timestamp to encode (epoch seconds).
 * @param out_buf      Output buffer (>= PAYLOAD_LEN_BYTES).
 * @param out_counter  Optional; receives new counter value.
 *
 * @return 0 on success, -EINVAL on invalid args.
 */
int payload_gen_build_button(uint8_t button_id, uint32_t epoch_s,
                             uint8_t *out_buf, uint32_t *out_counter);

/**
 * @brief Build counter-sync payload (Event 0x07) for one button after rejoin.
 * Does not increment; uses current counter from store.
 */
int payload_gen_build_counter_sync(uint8_t button_id, uint32_t epoch_s,
                                   uint8_t *out_buf);

/**
 * @brief Build battery status payload (Event 0x10) for housekeeping.
 *
 * Payload format (11 bytes):
 *  - [0..3] timestamp (epoch seconds, big-endian)
 *  - [4]    event type (EVT_BATTERY_STATUS)
 *  - [5..6] battery millivolts (big-endian)
 *  - [7]    battery percent (0..100, caller-provided; 0 if unknown)
 *  - [8]    flags (bitfield; 0 for now)
 *  - [9..10] reserved
 */
int payload_gen_build_battery_status(uint32_t epoch_s, uint16_t battery_mv,
                                     uint8_t percent, uint8_t flags,
                                     uint8_t *out_buf);

/**
 * @brief Build NFC check-in payload (Event 0x01).
 * Format: [0..3] ts, [4] EVT_NFC_IN, [5..8] 4-byte card data, [9..10] reserved.
 */
int payload_gen_build_nfc_in(uint32_t epoch_s, const uint8_t *data_4,
                             uint8_t *out_buf);

/**
 * @brief Build NFC check-out payload (Event 0x02).
 * Format: [0..3] ts, [4] EVT_NFC_OUT, [5..8] 4-byte card data, [9..10]
 * reserved.
 */
int payload_gen_build_nfc_out(uint32_t epoch_s, const uint8_t *data_4,
                              uint8_t *out_buf);

/**
 * @brief Build NFC vote payload (Event 0x03).
 * Format: [0..3] ts, [4] EVT_NFC_VOTE, [5] button_id, [6..9] 4-byte data,
 * [10] reserved.
 */
int payload_gen_build_nfc_vote(uint32_t epoch_s, uint8_t button_id,
                               const uint8_t *data_4, uint8_t *out_buf);

/* To extend: add enum payload_event_type, FPort, and a build_* function. */

#ifdef __cplusplus
}
#endif

#endif /* PAYLOAD_GEN_H */
