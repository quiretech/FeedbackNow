#ifndef BUTTONS_H
#define BUTTONS_H

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#define NUM_BUTTONS 7

extern const struct gpio_dt_spec buttons[NUM_BUTTONS];

typedef enum { BUTTON_EVENT_PRESS, BUTTON_EVENT_RELEASE } button_event_type_t;

typedef struct {
  uint8_t button_id;
  button_event_type_t type;
  int64_t timestamp_ms;
} button_event_t;

int buttons_init(void);
bool buttons_get_event(button_event_t *event, k_timeout_t timeout);

#endif // BUTTONS_H