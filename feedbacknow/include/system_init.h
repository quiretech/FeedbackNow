#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include <zephyr/kernel.h>

// Initialize all peripherals (buttons, leds, lora, etc)
int system_init(void);

#endif // SYSTEM_INIT_H
