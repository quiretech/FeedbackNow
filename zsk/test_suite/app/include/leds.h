#ifndef LEDS_H
#define LEDS_H

#include <stdbool.h>

int leds_init(void);
int led_set(int led_idx, bool val);

#endif /* LEDS_H */
