#ifndef LEDS_H
#define LEDS_H

#include <stdbool.h>

int leds_init(void);
int led_set(int led_idx, bool val);
int led_toggle(int led_idx);

#endif /* LEDS_H */


