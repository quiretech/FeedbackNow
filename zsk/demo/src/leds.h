#ifndef LEDS_H
#define LEDS_H

#include <stdbool.h>
#include <zephyr/drivers/gpio.h>

#define NUM_LEDS 1

// LED GPIO specs - will be populated from device tree
extern const struct gpio_dt_spec leds[NUM_LEDS];

int leds_init(void);
int led_set(int led_id, bool state);
int led_toggle(int led_id);

#endif /* LEDS_H */
