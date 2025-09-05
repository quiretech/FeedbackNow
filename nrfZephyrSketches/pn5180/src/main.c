#include "pn5180.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {
  LOG_INF("PN5180 ISO15693 Inventory Test Starting...");

  /* 1. Initialize PN5180 GPIO + SPI */
  pn5180_init();

  /* 2. Optional: run GPIO test pattern to verify pins */

  /* 3. Prepare buffer for tag UID */
  uint8_t uid[8] = {0};

  /* 4. Perform ISO15693 inventory */

  LOG_INF("PN5180 ISO15693 test complete.");

  /* 6. Heartbeat loop */
  while (1) {
    // k_msleep(1000);
    if (pn5180_get_inventory(uid)) {
      LOG_INF("Tag detected! UID:");
      LOG_HEXDUMP_INF(uid, sizeof(uid), "UID");
    } else {
      LOG_WRN("No tag detected.");
    }
  }
}