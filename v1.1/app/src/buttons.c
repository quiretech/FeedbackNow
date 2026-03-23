/**
 * Low-level button driver: GPIO, both-edge interrupts, per-button debounce.
 * Emits press and release events so the input layer can track held set and
 * combos.
 */
#include "buttons.h"
#include "sys_config.h"
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(buttons, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)
#define SW1_NODE DT_ALIAS(sw1)
#define SW2_NODE DT_ALIAS(sw2)
#define SW3_NODE DT_ALIAS(sw3)
#define SW4_NODE DT_ALIAS(sw4)
#define SW5_NODE DT_ALIAS(sw5)

K_MSGQ_DEFINE(button_msgq, sizeof(button_event_t), BUTTON_QUEUE_SIZE,
              BUTTON_QUEUE_ALIGNMENT);

const struct gpio_dt_spec buttons[NUM_BUTTONS] = {
    GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW2_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW3_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW4_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW5_NODE, gpios, {0}),
};

static struct gpio_callback button_cb_data[NUM_BUTTONS];
static struct k_timer debounce_timer[NUM_BUTTONS];

static void button_isr(const struct device *dev, struct gpio_callback *cb,
                       uint32_t pins);

static void debounce_expiry(struct k_timer *timer) {
  intptr_t i = (intptr_t)k_timer_user_data_get(timer);
  int val = gpio_pin_get_dt(&buttons[i]);
  /* Active-high: 1 = pressed, 0 = released */
  button_event_t evt = {
      .button_id = (uint8_t)i,
      .type = val ? BUTTON_EVENT_PRESS : BUTTON_EVENT_RELEASE,
      .timestamp_ms = k_uptime_get(),
  };
  int qret = k_msgq_put(&button_msgq, &evt, K_NO_WAIT);
  if (qret != 0) {
    LOG_WRN("button_msgq full, dropping event btn=%u type=%u", evt.button_id,
            evt.type);
  }
}

bool buttons_get_event(button_event_t *event, k_timeout_t timeout) {
  return k_msgq_get(&button_msgq, event, timeout) == 0;
}

uint32_t buttons_get_held_mask(void) {
  uint32_t mask = 0;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (gpio_pin_get_dt(&buttons[i]) == 1) { /* active-high: 1 = pressed */
      mask |= (1U << i);
    }
  }
  return mask;
}

static void button_isr(const struct device *dev, struct gpio_callback *cb,
                       uint32_t pins) {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (buttons[i].port == dev && (pins & BIT(buttons[i].pin))) {
      k_timer_start(&debounce_timer[i], K_MSEC(BUTTON_DEBOUNCE_MS), K_NO_WAIT);
    }
  }
}

int buttons_init(void) {
  int ret;

  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (!gpio_is_ready_dt(&buttons[i])) {
      LOG_ERR("Button %d device not ready", i);
      return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
    if (ret != 0) {
      LOG_ERR("Failed to configure button %d", i);
      return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_EDGE_BOTH);
    if (ret != 0) {
      LOG_ERR("Failed to set interrupt for button %d", i);
      return ret;
    }

    k_timer_init(&debounce_timer[i], debounce_expiry, NULL);
    k_timer_user_data_set(&debounce_timer[i], (void *)(intptr_t)i);

    gpio_init_callback(&button_cb_data[i], button_isr, BIT(buttons[i].pin));
    gpio_add_callback(buttons[i].port, &button_cb_data[i]);

    LOG_INF("Button %d on %s pin %d", i, buttons[i].port->name, buttons[i].pin);
  }

  return 0;
}
