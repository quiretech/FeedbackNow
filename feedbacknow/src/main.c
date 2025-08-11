
#include "buttons.h"
#include "leds.h"
#include "system_init.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {
  system_init();

  while (1) {
    k_sleep(K_SECONDS(1));
  }
}
