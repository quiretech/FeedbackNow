#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

// LED command types
typedef enum {
  LED_CMD_ON,
  LED_CMD_OFF,
  LED_CMD_TOGGLE,
  LED_CMD_BLINK_ONCE // New: blink once for 1 second
} led_cmd_type_t;

// LED command structure
typedef struct {
  uint8_t led_id;
  led_cmd_type_t cmd;
  uint32_t duration_ms; // For future use
} led_cmd_t;

// LED manager functions
int led_manager_init(void);
int led_manager_send_command(const led_cmd_t *cmd);
int led_manager_blink_once(uint8_t led_id);
int led_manager_set_led(uint8_t led_id, bool state);

// LED command queue
extern struct k_msgq led_cmd_queue;

// LED manager thread function
void led_manager_thread(void *a, void *b, void *c);

#endif // LED_MANAGER_H
