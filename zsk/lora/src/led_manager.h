#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

int led_manager_init(void);
int led_manager_set_led(uint8_t led_id, bool state);
int led_manager_blink_once(uint8_t led_id);

#endif /* LED_MANAGER_H */


