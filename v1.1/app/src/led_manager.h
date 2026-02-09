/**
 * LED UI layer (RTOS-friendly).
 *
 * Single LED thread owns all GPIO and timing. Other threads only post commands
 * via a message queue; they never block on LED or touch GPIO. Patterns (blink
 * once, join success, etc.) are executed inside the LED thread.
 */
#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/** Call from any thread; non-blocking. Returns 0 or -ENOMEM if queue full. */
int led_manager_init(void);

/** Set LED on or off (solid until next command). */
int led_manager_set_led(uint8_t led_id, bool state);

/** One short blink (on for LED_BLINK_DURATION_MS then off). Queued, non-blocking. */
int led_manager_blink_once(uint8_t led_id);

/** Pattern: 5× 200ms on/off (e.g. join success). Queued, non-blocking. */
int led_manager_pattern_join_success(uint8_t led_id);

#endif /* LED_MANAGER_H */
