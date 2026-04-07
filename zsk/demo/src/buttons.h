#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define NUM_BUTTONS 7
#define BUTTON_QUEUE_SIZE 10
#define BUTTON_QUEUE_ALIGNMENT 4

typedef enum {
  BUTTON_EVENT_PRESS = 0,
  BUTTON_EVENT_RELEASE = 1
} button_event_type_t;

typedef struct {
  uint8_t button_id;
  button_event_type_t type;
  int64_t timestamp_ms;
} button_event_t;

int buttons_init(void);
bool buttons_get_event(button_event_t *event, k_timeout_t timeout);

#endif /* BUTTONS_H */
