#include "buttons.h"
#include "leds.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {
  int ret;

  LOG_INF("Starting nRF52840 Button/LED Project");

  ret = button_handler_init();
  if (ret != 0) {
    LOG_ERR("Button handler init failed (%d)", ret);
    return ret;
  }

  ret = leds_init();
  if (ret != 0) {
    LOG_ERR("LEDs handler init failed (%d)", ret);
    return ret;
  }

  while (1) {
    /* For now, just sleep — button interrupts will trigger callbacks */
    k_sleep(K_MSEC(500));
  }

  return 0;
}
