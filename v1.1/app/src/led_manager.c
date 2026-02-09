/**
 * LED UI: single thread + message queue. All LED timing and GPIO happen here;
 * callers only post commands and return immediately.
 */
#include "led_manager.h"
#include "leds.h"
#include "sys_config.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_manager, CONFIG_LOG_DEFAULT_LEVEL);

/* Command types (internal) */
enum led_ui_cmd {
  LED_UI_CMD_OFF,
  LED_UI_CMD_ON,
  LED_UI_CMD_BLINK_ONCE,
  LED_UI_CMD_PATTERN_JOIN_SUCCESS,
};

#define LED_UI_MSGQ_LEN 8
#define LED_UI_MSGQ_ALIGN 4

typedef struct {
  uint8_t led_id;
  uint8_t cmd;
} led_ui_msg_t;

K_MSGQ_DEFINE(led_ui_msgq, sizeof(led_ui_msg_t), LED_UI_MSGQ_LEN, LED_UI_MSGQ_ALIGN);

/* Per-LED pattern state (only LED 0 used for now) */
#define LED_UI_JOIN_BLINKS  5
#define LED_UI_JOIN_ON_MS   200
#define LED_UI_JOIN_OFF_MS  200

static struct {
  enum led_ui_cmd cmd;
  uint8_t step;
  int64_t next_ms;
} led_ui_state[NUM_LEDS];

#define LED_UI_THREAD_STACK 1024
#define LED_UI_THREAD_PRIORITY 9

static void led_ui_apply(uint8_t led_id, bool on) {
  if (led_id >= NUM_LEDS) {
    return;
  }
  (void)led_set(led_id, on);
}

static void led_ui_thread_fn(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  k_thread_name_set(k_current_get(), "led_ui");
  LOG_INF("LED UI thread started");

  for (int i = 0; i < NUM_LEDS; i++) {
    led_ui_state[i].cmd = LED_UI_CMD_OFF;
    led_ui_state[i].step = 0;
    led_ui_state[i].next_ms = 0;
  }

  while (1) {
    int64_t now_ms = k_uptime_get();
    int64_t next_wake_ms = 0;

    for (int i = 0; i < NUM_LEDS; i++) {
      if (led_ui_state[i].next_ms > 0 && led_ui_state[i].cmd != LED_UI_CMD_OFF &&
          led_ui_state[i].cmd != LED_UI_CMD_ON) {
        if (next_wake_ms == 0 || led_ui_state[i].next_ms < next_wake_ms) {
          next_wake_ms = led_ui_state[i].next_ms;
        }
      }
    }

    k_timeout_t timeout = K_FOREVER;
    if (next_wake_ms > 0) {
      int64_t delta = next_wake_ms - now_ms;
      if (delta > 0) {
        timeout = K_MSEC((int32_t)(delta > 2147483647 ? 2147483647 : delta));
      }
    }

    led_ui_msg_t msg;
    int ret = k_msgq_get(&led_ui_msgq, &msg, timeout);

    now_ms = k_uptime_get();

    /* Handle timeout: advance pattern for any LED that is due */
    if (ret != 0) {
      for (int i = 0; i < NUM_LEDS; i++) {
        if (led_ui_state[i].next_ms == 0 || now_ms < led_ui_state[i].next_ms) {
          continue;
        }
        if (led_ui_state[i].cmd == LED_UI_CMD_BLINK_ONCE) {
          led_ui_apply(i, false);
          led_ui_state[i].cmd = LED_UI_CMD_OFF;
          led_ui_state[i].next_ms = 0;
          LOG_DBG("LED %d blink_once done", i);
        } else if (led_ui_state[i].cmd == LED_UI_CMD_PATTERN_JOIN_SUCCESS) {
          if (led_ui_state[i].step & 1) {
            led_ui_apply(i, false);
            led_ui_state[i].step++;
            if (led_ui_state[i].step >= (LED_UI_JOIN_BLINKS * 2)) {
              led_ui_state[i].cmd = LED_UI_CMD_OFF;
              led_ui_state[i].next_ms = 0;
              LOG_DBG("LED %d join pattern done", i);
            } else {
              led_ui_state[i].next_ms = now_ms + LED_UI_JOIN_OFF_MS;
            }
          } else {
            led_ui_apply(i, true);
            led_ui_state[i].step++;
            led_ui_state[i].next_ms = now_ms + LED_UI_JOIN_ON_MS;
          }
        }
      }
      continue;
    }

    /* New command */
    if (msg.led_id >= NUM_LEDS) {
      continue;
    }
    uint8_t id = msg.led_id;
    enum led_ui_cmd cmd = (enum led_ui_cmd)msg.cmd;
    led_ui_state[id].cmd = cmd;
    led_ui_state[id].step = 0;
    led_ui_state[id].next_ms = 0;

    switch (cmd) {
    case LED_UI_CMD_OFF:
      led_ui_apply(id, false);
      break;
    case LED_UI_CMD_ON:
      led_ui_apply(id, true);
      break;
    case LED_UI_CMD_BLINK_ONCE:
      led_ui_apply(id, true);
      led_ui_state[id].next_ms = now_ms + LED_BLINK_DURATION_MS;
      break;
    case LED_UI_CMD_PATTERN_JOIN_SUCCESS:
      led_ui_apply(id, true);
      led_ui_state[id].step = 1;
      led_ui_state[id].next_ms = now_ms + LED_UI_JOIN_ON_MS;
      break;
    default:
      led_ui_apply(id, false);
      break;
    }
  }
}

K_THREAD_DEFINE(led_ui_thread_id, LED_UI_THREAD_STACK, led_ui_thread_fn, NULL,
                NULL, NULL, LED_UI_THREAD_PRIORITY, 0, 0);

static int led_ui_post(uint8_t led_id, enum led_ui_cmd cmd) {
  if (led_id >= NUM_LEDS) {
    return -EINVAL;
  }
  led_ui_msg_t msg = { .led_id = led_id, .cmd = cmd };
  return k_msgq_put(&led_ui_msgq, &msg, K_NO_WAIT) == 0 ? 0 : -ENOMEM;
}

int led_manager_init(void) {
  int ret = leds_init();
  if (ret != 0) {
    LOG_ERR("LED init failed: %d", ret);
    return ret;
  }
  LOG_INF("LED manager initialized (queue + thread)");
  return 0;
}

int led_manager_set_led(uint8_t led_id, bool state) {
  return led_ui_post(led_id, state ? LED_UI_CMD_ON : LED_UI_CMD_OFF);
}

int led_manager_blink_once(uint8_t led_id) {
  return led_ui_post(led_id, LED_UI_CMD_BLINK_ONCE);
}

int led_manager_pattern_join_success(uint8_t led_id) {
  return led_ui_post(led_id, LED_UI_CMD_PATTERN_JOIN_SUCCESS);
}
