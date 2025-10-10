#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(epd_main, LOG_LEVEL_INF);

int main(void) {
  LOG_INF(
      "Application started - display driver temporarily disabled for testing");

  // Simple test to verify basic functionality
  for (int i = 0; i < 5; i++) {
    LOG_INF("Test iteration %d", i);
    k_msleep(1000);
  }

  LOG_INF("Basic test completed successfully");
  return 0;
}