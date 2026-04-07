#ifndef PAYLOAD_GEN_H
#define PAYLOAD_GEN_H

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

/* Fixed payload size for DR0 US915 */
#define PAYLOAD_LEN_BYTES 11

/* Event types */
enum payload_event_type {
  EVT_BUTTON = 0x00,
  EVT_NFC = 0x01,
  EVT_BATTERY = 0x02,
  EVT_FUTURE = 0xFF, /* placeholder for future extensions */
};

/* LoRaWAN FPorts */
#define FPORT_BUTTON 10
#define FPORT_NFC 11
#define FPORT_BATTERY 12
#define FPORT_FUTURE 13

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize generator state (counters, epoch seed).
 */
void payload_gen_init(void);

/**
 * @brief Build next payload in rotating event order.
 *
 * @param out_buf   Buffer of at least PAYLOAD_LEN_BYTES.
 * @param out_fport Returns selected FPort.
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int payload_gen_next(uint8_t *out_buf, uint8_t *out_fport);

/* Debug helpers */
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
 * @param button_id    Button ID to encode (0..6 expected by backend).
 * @param epoch_s      Timestamp to encode (epoch seconds).
 * @param out_buf      Output buffer (>= PAYLOAD_LEN_BYTES).
 * @param out_counter  Optional; receives new counter value.
 *
 * @return 0 on success, -EINVAL on invalid args.
 */
int payload_gen_build_button(uint8_t button_id, uint32_t epoch_s,
                             uint8_t *out_buf, uint32_t *out_counter);

/* To extend with new event types:
 * 1) Add a new enum payload_event_type value and FPort define.
 * 2) Implement a build_* helper that fills bytes 5..10.
 * 3) Add a case in payload_gen_next to call your builder and set the FPort.
 */

#ifdef __cplusplus
}
#endif

#endif /* PAYLOAD_GEN_H */
