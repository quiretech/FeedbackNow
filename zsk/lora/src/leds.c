#include "leds.h"
#include "led_manager.h"
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(leds, LOG_LEVEL_INF);

/* Pull GPIO spec from DT alias - single LED */
#define LED0_NODE DT_ALIAS(led0)

// LED command queue is now defined in led_manager.c

/* Array of LED GPIO specs - now just one LED */
const struct gpio_dt_spec leds[NUM_LEDS] = {
    GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, {0}),
};

int leds_init(void) {
  int ret;
  for (int i = 0; i < NUM_LEDS; i++) {
    if (!device_is_ready(leds[i].port)) {
      LOG_ERR("LED %d device not ready", i);
      return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
      LOG_ERR("Failed to configure LED %d pin", i);
      return ret;
    }
    LOG_INF("Initialized LED %d on %s pin %d", i, leds[i].port->name,
            leds[i].pin);
  }
  return 0;
}

int led_set(int led_idx, bool val) {
  if (led_idx < 0 || led_idx >= NUM_LEDS) {
    return -EINVAL;
  }
  return gpio_pin_set_dt(&leds[led_idx], val);
}

int led_toggle(int led_idx) {
  if (led_idx < 0 || led_idx >= NUM_LEDS) {
    return -EINVAL;
  }
  return gpio_pin_toggle_dt(&leds[led_idx]);
}

// LED command functions are now in led_manager.c
