#ifndef BUTTONS_H
#define BUTTONS_H

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#define NUM_BUTTONS 7

extern const struct gpio_dt_spec buttons[NUM_BUTTONS];

int button_handler_init(void);

#endif // BUTTONS_H