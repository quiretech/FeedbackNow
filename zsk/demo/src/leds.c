#include "leds.h"
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(leds, LOG_LEVEL_INF);

/* Pull GPIO spec from DT alias */
#define LED0_NODE DT_ALIAS(led0)

/* Array of LED GPIO specs */
const struct gpio_dt_spec leds[NUM_LEDS] = {
    GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, {0}),
};

int leds_init(void) {
  int ret;

  for (int i = 0; i < NUM_LEDS; i++) {
    if (!gpio_is_ready_dt(&leds[i])) {
      LOG_ERR("LED %d device %s not ready", i, leds[i].port->name);
      return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
      LOG_ERR("Failed to configure LED %d", i);
      return ret;
    }

    LOG_INF("Initialized LED %d on %s pin %d", i, leds[i].port->name,
            leds[i].pin);
  }

  return 0;
}

int led_set(int led_id, bool state) {
  if (led_id >= NUM_LEDS) {
    return -EINVAL;
  }

  return gpio_pin_set_dt(&leds[led_id], state);
}

int led_toggle(int led_id) {
  if (led_id >= NUM_LEDS) {
    return -EINVAL;
  }

  return gpio_pin_toggle_dt(&leds[led_id]);
}
