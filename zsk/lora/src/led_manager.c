#include "led_manager.h"
#include "leds.h"
#include "sys_config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_manager, LOG_LEVEL_INF);

/* LED blink work items - one per LED */
static struct k_work led_blink_work[NUM_LEDS];
static struct k_timer led_blink_timer[NUM_LEDS];

/* LED blink work handler - single LED */
static void led_blink_work_handler_0(struct k_work *work) { led_set(0, false); }

static void (*led_blink_handlers[NUM_LEDS])(struct k_work *) = {
    led_blink_work_handler_0,
};

/* Timer handler for LED blink - single LED */
static void led_blink_timer_handler_0(struct k_timer *timer) {
  k_work_submit(&led_blink_work[0]);
}

static void (*led_timer_handlers[NUM_LEDS])(struct k_timer *) = {
    led_blink_timer_handler_0,
};

int led_manager_init(void) {
  // Initialize LED hardware
  int ret = leds_init();
  if (ret != 0) {
    LOG_ERR("Failed to initialize LEDs: %d", ret);
    return ret;
  }

  // Initialize work items and timers for each LED
  for (int i = 0; i < NUM_LEDS; i++) {
    k_work_init(&led_blink_work[i], led_blink_handlers[i]);
    k_timer_init(&led_blink_timer[i], led_timer_handlers[i], NULL);
  }

  LOG_INF("LED manager initialized");
  return 0;
}

int led_manager_blink_once(uint8_t led_id) {
  if (led_id >= NUM_LEDS) {
    LOG_ERR("Invalid LED ID: %d", led_id);
    return -EINVAL;
  }

  // Turn on LED
  int ret = led_set(led_id, true);
  if (ret != 0) {
    LOG_ERR("Failed to turn on LED %d: %d", led_id, ret);
    return ret;
  }

  // Start timer to turn off after 1 second
  k_timer_start(&led_blink_timer[led_id], K_MSEC(LED_BLINK_DURATION_MS),
                K_NO_WAIT);

  LOG_INF("LED %d blinking for 1 second", led_id);
  return 0;
}

int led_manager_set_led(uint8_t led_id, bool state) {
  if (led_id >= NUM_LEDS) {
    LOG_ERR("Invalid LED ID: %d", led_id);
    return -EINVAL;
  }

  int ret = led_set(led_id, state);
  if (ret != 0) {
    LOG_ERR("Failed to set LED %d to %s: %d", led_id, state ? "ON" : "OFF",
            ret);
    return ret;
  }

  LOG_DBG("LED %d set to %s", led_id, state ? "ON" : "OFF");
  return 0;
}
