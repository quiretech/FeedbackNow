#include "pn5180.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Get PN5180 device from device tree */
#define PN5180_NODE DT_NODELABEL(pn5180)
static const struct device *pn5180_dev = DEVICE_DT_GET(PN5180_NODE);

int main(void) {
  LOG_INF("PN5180 ISO15693 Inventory Test Starting...");

  /* Check if device is ready */
  if (!device_is_ready(pn5180_dev)) {
    LOG_ERR("PN5180 device not ready");
    return -ENODEV;
  }

  /* Initialize the driver */
  if (pn5180_init(pn5180_dev) != 0) {
    LOG_ERR("Failed to initialize PN5180");
    return -EIO;
  }

  LOG_INF("PN5180 initialized and configured");

  /* Main loop */
  uint8_t uid[8] = {0};
  while (1) {
    if (pn5180_get_inventory(pn5180_dev, uid, sizeof(uid)) == 0) {
      LOG_INF("Tag detected! UID:");
      LOG_HEXDUMP_INF(uid, sizeof(uid), "UID");
    } else {
      LOG_WRN("No tag detected.");
    }
    k_msleep(1000);
  }
}