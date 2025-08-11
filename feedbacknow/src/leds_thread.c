#include "k_config.h"
#include "leds.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_thread, LOG_LEVEL_INF);

/* LED control thread function */
void led_thread_func(void *, void *, void *);
K_THREAD_DEFINE(led_thread_id, LED_THREAD_STACK_SIZE, led_thread_func, NULL,
                NULL, NULL, LED_THREAD_PRIORITY, 0, 0);

void led_thread_func(void *a, void *b, void *c) {
  led_cmd_t cmd;
  while (1) {
    LOG_INF("LED thread waiting for command...");
    if (leds_get_command(&cmd, K_FOREVER)) {
      LOG_INF("LED command: id %d, cmd %d", cmd.led_id, cmd.cmd);
      led_set(cmd.led_id, cmd.cmd == LED_ON);
    }
  }
}