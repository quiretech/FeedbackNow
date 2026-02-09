#ifndef BUTTONS_H
#define BUTTONS_H

#include "sys_config.h"
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

// NUM_BUTTONS is now defined in sys_config.h

extern const struct gpio_dt_spec buttons[NUM_BUTTONS];

typedef enum { BUTTON_EVENT_PRESS, BUTTON_EVENT_RELEASE } button_event_type_t;

typedef struct {
  uint8_t button_id;
  button_event_type_t type;
  int64_t timestamp_ms;
} button_event_t;

int buttons_init(void);
bool buttons_get_event(button_event_t *event, k_timeout_t timeout);
/** Current mask of pressed buttons (bit i set = button i pressed). Use to sync held state. */
uint32_t buttons_get_held_mask(void);

#endif // BUTTONS_H
