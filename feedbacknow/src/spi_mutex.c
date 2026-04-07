#include "spi_mutex.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi_mutex, LOG_LEVEL_INF);

// Shared SPI bus mutex for thread safety between LoRa and NFC devices
struct k_mutex spi_bus_mutex;

int spi_mutex_init(void) {
  k_mutex_init(&spi_bus_mutex);
  LOG_INF("SPI bus mutex initialized");
  return 0;
}

int spi_mutex_lock(k_timeout_t timeout) {
  return k_mutex_lock(&spi_bus_mutex, timeout);
}

void spi_mutex_unlock(void) { k_mutex_unlock(&spi_bus_mutex); }
