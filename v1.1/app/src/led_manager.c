/**
 * LED UI thread: event-driven patterns. One thread, one msgq; messages are
 * (led_id, pattern_id). All patterns are on/off or timed blinks (no PWM).
 */
#include "led_manager.h"
#include "leds.h"
#include "sys_config.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_manager, CONFIG_LOG_DEFAULT_LEVEL);

#define LED_UI_MSGQ_LEN 8
#define LED_UI_MSGQ_ALIGN 4

typedef struct {
  uint8_t led_id;
  uint8_t pattern;
} led_ui_msg_t;

K_MSGQ_DEFINE(led_ui_msgq, sizeof(led_ui_msg_t), LED_UI_MSGQ_LEN,
              LED_UI_MSGQ_ALIGN);

/* Per-LED runner state */
static struct {
  uint8_t pattern;
  uint8_t step; /* cycle index for finite patterns */
  int64_t next_ms;
} led_state[NUM_LEDS];

#define LED_UI_THREAD_STACK 1024
#define LED_UI_THREAD_PRIORITY 9

static void led_apply(uint8_t led_id, bool on) {
  if (led_id >= NUM_LEDS) {
    return;
  }
  (void)led_set(led_id, on);
}

/* Advance a finite blink pattern: (on_ms, off_ms) × count, then OFF */
static bool advance_finite(uint8_t id, int64_t now_ms, uint8_t count,
                           uint32_t on_ms, uint32_t off_ms) {
  if (led_state[id].step == 0) {
    led_apply(id, true);
    led_state[id].next_ms = now_ms + on_ms;
    led_state[id].step = 1;
    return true;
  }
  if (led_state[id].step < (count * 2)) {
    bool is_on = (led_state[id].step & 1) != 0;
    led_apply(id, !is_on);
    led_state[id].step++;
    led_state[id].next_ms = now_ms + (is_on ? on_ms : off_ms);
    return true;
  }
  led_apply(id, false);
  led_state[id].pattern = LED_PATTERN_OFF;
  led_state[id].step = 0;
  led_state[id].next_ms = 0;
  return false;
}

/* 1 Hz repeat: 500 on, 500 off until next command */
static void advance_1hz(uint8_t id, int64_t now_ms) {
  bool currently_on = (led_state[id].step & 1) != 0;
  led_apply(id, !currently_on);
  led_state[id].step++;
  led_state[id].next_ms =
      now_ms + (currently_on ? LED_NFC_1HZ_ON_MS : LED_NFC_1HZ_OFF_MS);
}

static void run_timeout(uint8_t id, int64_t now_ms) {
  if (led_state[id].next_ms == 0 || now_ms < led_state[id].next_ms) {
    return;
  }
  switch (led_state[id].pattern) {
  case LED_PATTERN_BUTTON_ACCEPTED:
    /* Step 1 = solid on started; after timeout turn off */
    if (led_state[id].step != 0) {
      led_apply(id, false);
      led_state[id].pattern = LED_PATTERN_OFF;
      led_state[id].step = 0;
      led_state[id].next_ms = 0;
    }
    break;
  case LED_PATTERN_JOIN_SUCCESS:
    advance_finite(id, now_ms, LED_JOIN_BLINKS, LED_JOIN_ON_MS,
                   LED_JOIN_OFF_MS);
    break;
  case LED_PATTERN_NFC_FAIL:
    advance_finite(id, now_ms, LED_NFC_FAIL_BLINKS, LED_NFC_FAIL_ON_MS,
                   LED_NFC_FAIL_OFF_MS);
    break;
  case LED_PATTERN_CONFIRM:
    advance_finite(id, now_ms, LED_CONFIRM_BLINKS, LED_CONFIRM_ON_MS,
                   LED_CONFIRM_OFF_MS);
    break;
  case LED_PATTERN_POWER_ON:
    advance_finite(id, now_ms, LED_POWER_ON_BLINKS, LED_POWER_ON_MS,
                   LED_POWER_ON_MS);
    break;
  case LED_PATTERN_REBOOT_HOLD:
    led_apply(id, false);
    led_state[id].pattern = LED_PATTERN_OFF;
    led_state[id].next_ms = 0;
    break;
  case LED_PATTERN_NFC_WAITING:
    advance_1hz(id, now_ms);
    break;
  default:
    break;
  }
}

static void start_pattern(uint8_t id, enum led_pattern_id pattern,
                          int64_t now_ms) {
  led_state[id].pattern = pattern;
  led_state[id].step = 0;
  led_state[id].next_ms = 0;

  switch (pattern) {
  case LED_PATTERN_OFF:
    led_apply(id, false);
    break;
  case LED_PATTERN_ON:
  case LED_PATTERN_REBOOT_HOLD:
    led_apply(id, true);
    if (pattern == LED_PATTERN_REBOOT_HOLD) {
      led_state[id].next_ms = now_ms + LED_REBOOT_HOLD_MS;
    }
    break;
  case LED_PATTERN_BUTTON_ACCEPTED:
    led_apply(id, true);
    led_state[id].next_ms = now_ms + LED_BUTTON_ACCEPTED_MS;
    led_state[id].step = 1;
    break;
  case LED_PATTERN_JOIN_SUCCESS:
  case LED_PATTERN_NFC_FAIL:
  case LED_PATTERN_CONFIRM:
  case LED_PATTERN_POWER_ON:
    advance_finite(id, now_ms,
                   pattern == LED_PATTERN_JOIN_SUCCESS ? LED_JOIN_BLINKS
                   : pattern == LED_PATTERN_NFC_FAIL   ? LED_NFC_FAIL_BLINKS
                   : pattern == LED_PATTERN_CONFIRM    ? LED_CONFIRM_BLINKS
                                                       : LED_POWER_ON_BLINKS,
                   pattern == LED_PATTERN_JOIN_SUCCESS ? LED_JOIN_ON_MS
                   : pattern == LED_PATTERN_NFC_FAIL   ? LED_NFC_FAIL_ON_MS
                   : pattern == LED_PATTERN_CONFIRM    ? LED_CONFIRM_ON_MS
                                                       : LED_POWER_ON_MS,
                   pattern == LED_PATTERN_JOIN_SUCCESS ? LED_JOIN_OFF_MS
                   : pattern == LED_PATTERN_NFC_FAIL   ? LED_NFC_FAIL_OFF_MS
                   : pattern == LED_PATTERN_CONFIRM    ? LED_CONFIRM_OFF_MS
                                                       : LED_POWER_ON_MS);
    break;
  case LED_PATTERN_NFC_WAITING:
    led_apply(id, true);
    led_state[id].step = 1;
    led_state[id].next_ms = now_ms + LED_NFC_1HZ_ON_MS;
    break;
  default:
    led_apply(id, false);
    break;
  }
}

static void led_ui_thread_fn(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  k_thread_name_set(k_current_get(), "led_ui");
  LOG_INF("LED UI thread started");

  for (int i = 0; i < NUM_LEDS; i++) {
    led_state[i].pattern = LED_PATTERN_OFF;
    led_state[i].step = 0;
    led_state[i].next_ms = 0;
  }

  while (1) {
    int64_t now_ms = k_uptime_get();
    int64_t next_ms = 0;

    for (int i = 0; i < NUM_LEDS; i++) {
      if (led_state[i].next_ms > 0) {
        if (next_ms == 0 || led_state[i].next_ms < next_ms) {
          next_ms = led_state[i].next_ms;
        }
      }
    }

    k_timeout_t timeout = K_FOREVER;
    if (next_ms > 0) {
      int64_t delta = next_ms - now_ms;
      if (delta > 0) {
        timeout = K_MSEC((int32_t)(delta > 2147483647 ? 2147483647 : delta));
      }
    }

    led_ui_msg_t msg;
    int ret = k_msgq_get(&led_ui_msgq, &msg, timeout);
    now_ms = k_uptime_get();

    if (ret != 0) {
      for (int i = 0; i < NUM_LEDS; i++) {
        run_timeout(i, now_ms);
      }
      continue;
    }

    if (msg.led_id >= NUM_LEDS || msg.pattern >= LED_PATTERN_COUNT) {
      continue;
    }
    start_pattern(msg.led_id, (enum led_pattern_id)msg.pattern, now_ms);
  }
}

K_THREAD_DEFINE(led_ui_thread_id, LED_UI_THREAD_STACK, led_ui_thread_fn, NULL,
                NULL, NULL, LED_UI_THREAD_PRIORITY, 0, 0);

int led_manager_init(void) {
  int ret = leds_init();
  if (ret != 0) {
    LOG_ERR("LED init failed: %d", ret);
    return ret;
  }
  LOG_INF("LED manager initialized");
  return 0;
}

int led_manager_show(uint8_t led_id, enum led_pattern_id pattern) {
  if (led_id >= NUM_LEDS || pattern >= LED_PATTERN_COUNT) {
    return -EINVAL;
  }
  led_ui_msg_t msg = {.led_id = led_id, .pattern = (uint8_t)pattern};
  return k_msgq_put(&led_ui_msgq, &msg, K_NO_WAIT) == 0 ? 0 : -ENOMEM;
}
