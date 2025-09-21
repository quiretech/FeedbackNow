#ifndef LEDS_H
#define LEDS_H

#include "sys_config.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

// NUM_LEDS is now defined in sys_config.h

extern const struct gpio_dt_spec leds[NUM_LEDS];

// Initialize all LEDs; return 0 on success, negative error code on failure
int leds_init(void);

// Set LED state by index (0 to NUM_LEDS-1), val = 1 turn on, 0 turn off
int led_set(int led_idx, bool val);

// Toggle LED by index
int led_toggle(int led_idx);

#endif // LEDS_H