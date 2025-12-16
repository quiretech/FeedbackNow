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

/* To extend with new event types:
 * 1) Add a new enum payload_event_type value and FPort define.
 * 2) Implement a build_* helper that fills bytes 5..10.
 * 3) Add a case in payload_gen_next to call your builder and set the FPort.
 */

#ifdef __cplusplus
}
#endif

#endif /* PAYLOAD_GEN_H */
