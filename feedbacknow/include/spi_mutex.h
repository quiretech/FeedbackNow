#ifndef SPI_MUTEX_H
#define SPI_MUTEX_H

#include <zephyr/kernel.h>

// Shared SPI bus mutex for thread safety between LoRa and NFC devices
extern struct k_mutex spi_bus_mutex;

// Initialize the shared SPI mutex
int spi_mutex_init(void);

// Lock the SPI bus (with timeout)
int spi_mutex_lock(k_timeout_t timeout);

// Unlock the SPI bus
void spi_mutex_unlock(void);

#endif // SPI_MUTEX_H
