/**
 * LED UI — event-driven, RTOS-friendly.
 *
 * Single LED thread owns GPIO and timing. Callers post a pattern id and return.
 * All patterns are on/off or timed blinks (no PWM). Safe to call from any
 * thread.
 */
#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdint.h>
#include <zephyr/kernel.h>

/** Pattern ids (FRD 4.4). Post one of these; thread runs it and then goes idle
 * or repeats until next command. */
enum led_pattern_id {
  LED_PATTERN_OFF,
  LED_PATTERN_ON, /**< Solid on until next command (Staff, Reboot hold) */
  LED_PATTERN_BUTTON_ACCEPTED, /**< Solid 1s then off */
  LED_PATTERN_JOIN_SUCCESS,    /**< 3 quick flashes */
  LED_PATTERN_JOINING,         /**< 2s on, 1s off repeat (no EPD: show join in progress) */
  LED_PATTERN_NFC_WAITING,     /**< 1 Hz blink until next command */
  LED_PATTERN_NFC_FAIL,        /**< 3 fast blinks then off */
  LED_PATTERN_CONFIRM,     /**< 3 blinks ~2s (check-in/out/registered vote) */
  LED_PATTERN_REBOOT_HOLD, /**< Solid for REBOOT_MS then off */
  LED_PATTERN_POWER_ON,    /**< 2 quick flashes (e.g. at boot) */
  LED_PATTERN_COUNT
};

/** Thread ID for main to start (K_THREAD_DEFINE with delay = -1). */
extern const k_tid_t led_ui_thread_id;

/** Init GPIO. Call once from main; start led_ui_thread_id from main thread block. */
int led_manager_init(void);
int led_manager_wait_until_ready(k_timeout_t timeout);

/**
 * Show a pattern on the given LED. Non-blocking; queued.
 * @param led_id  LED index (0 .. NUM_LEDS-1)
 * @param pattern Pattern id (e.g. LED_PATTERN_OFF, LED_PATTERN_JOIN_SUCCESS)
 * @return 0 on success, -EINVAL if bad args, -ENOMEM if queue full
 */
int led_manager_show(uint8_t led_id, enum led_pattern_id pattern);

#endif /* LED_MANAGER_H */
