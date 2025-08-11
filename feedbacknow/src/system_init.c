#include "system_init.h"
#include "buttons.h"
#include "leds.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(SYS_INIT);

int system_init(void) {
  LOG_INF("System init start\n");
  int ret;

  ret = buttons_init();
  if (ret) {
    LOG_ERR("Buttons init failed: %d", ret);
    return ret;
  }

  ret = leds_init();
  if (ret) {
    LOG_ERR("LEDs init failed: %d", ret);
    return ret;
  }

  LOG_INF("\nSystem init complete");
  return 0;
}
