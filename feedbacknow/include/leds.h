#ifndef LEDS_H
#define LEDS_H

#include "k_config.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#define NUM_LEDS 7

typedef enum {
  LED_ON,
  LED_OFF,
} led_cmd_type_t;

typedef struct {
  uint8_t led_id;
  led_cmd_type_t cmd;
} led_cmd_t;

bool leds_get_command(led_cmd_t *cmd, k_timeout_t timeout);
extern struct k_msgq led_cmd_queue;
extern const struct gpio_dt_spec leds[NUM_LEDS];

// Initialize all LEDs; return 0 on success, negative error code on failure
int leds_init(void);

// Set LED state by index (0 to NUM_LEDS-1), val = 1 turn on, 0 turn off
int led_set(int led_idx, bool val);

// Toggle LED by index
int led_toggle(int led_idx);

#endif // LEDS_H