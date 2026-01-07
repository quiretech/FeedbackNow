#ifndef SDCARD_LOGGER_H
#define SDCARD_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize SD card (disk + mount FATFS at /SD:)
 *
 * Safe to call multiple times.
 */
int sdcard_logger_init(void);

/**
 * @brief Return true if SD is mounted and ready for writes.
 */
bool sdcard_logger_is_ready(void);

/**
 * @brief Append a downlink payload to /SD:/downlink.log
 *
 * Writes one line per downlink with uptime + metadata + hex payload.
 */
int sdcard_logger_append_downlink(uint8_t port, uint8_t flags, int16_t rssi,
                                  int8_t snr, const uint8_t *data, uint8_t len);

/**
 * @brief Queue a downlink for SD write in a safe context (worker thread).
 *
 * This avoids doing filesystem I/O inside the LoRaWAN downlink callback.
 */
int sdcard_logger_submit_downlink(uint8_t port, uint8_t flags, int16_t rssi,
                                  int8_t snr, const uint8_t *data, uint8_t len);

#endif /* SDCARD_LOGGER_H */
