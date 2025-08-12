#include "buttons.h"
#include "k_config.h"
#include "leds.h"
#include "nvs.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button_thread, LOG_LEVEL_INF);

extern struct k_msgq led_cmd_queue;

void button_thread_func(void *, void *, void *);
K_THREAD_DEFINE(button_thread_id, BUTTON_THREAD_STACK_SIZE, button_thread_func,
                NULL, NULL, NULL, BUTTON_THREAD_PRIORITY, 0, 0);

void button_thread_func(void *a, void *b, void *c) {
  button_event_t evt;
  led_cmd_t led_cmd;

  while (1) {
    LOG_INF("Button thread waiting for event...");
    if (buttons_get_event(&evt, K_FOREVER)) {
      LOG_INF("Button %d %s at %lld", evt.button_id,
              evt.type == BUTTON_EVENT_PRESS ? "pressed" : "released",
              evt.timestamp_ms);

      /* Simple logic: turn on LED with same ID when pressed, off when
      released
       */
      led_cmd.led_id = evt.button_id;
      led_cmd.cmd = (evt.type == BUTTON_EVENT_PRESS) ? LED_ON : LED_OFF;

      /* Send command to LED thread */
      k_msgq_put(&led_cmd_queue, &led_cmd, K_NO_WAIT);
    }
  }
}