#include "buttons.h"
#include "leds.h"
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(buttons, LOG_LEVEL_INF);

/* Pull GPIO spec from DT aliases */
#define SW0_NODE DT_ALIAS(sw0)
#define SW1_NODE DT_ALIAS(sw1)
#define SW2_NODE DT_ALIAS(sw2)
#define SW3_NODE DT_ALIAS(sw3)
#define SW4_NODE DT_ALIAS(sw4)
#define SW5_NODE DT_ALIAS(sw5)
#define SW6_NODE DT_ALIAS(sw6)

/* EVENT Q */
K_MSGQ_DEFINE(button_msgq, sizeof(button_event_t), BUTTON_QUEUE_SIZE,
              BUTTON_QUEUE_ALIGNMENT);

/* Array of button GPIO specs */
const struct gpio_dt_spec buttons[NUM_BUTTONS] = {
    GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW2_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW3_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW4_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW5_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW6_NODE, gpios, {0}),
};

/* Callback storage for each button */
static struct gpio_callback button_cb_data[NUM_BUTTONS];

/*Button ISR*/
static void button_isr(const struct device *dev, struct gpio_callback *cb,
                       uint32_t pins);

bool buttons_get_event(button_event_t *event, k_timeout_t timeout) {
  return k_msgq_get(&button_msgq, event, timeout) == 0;
}

/* Interrupt handler */
static void button_isr(const struct device *dev, struct gpio_callback *cb,
                       uint32_t pins) {
  int64_t now = k_uptime_get();
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (pins & BIT(buttons[i].pin)) {
      int val = gpio_pin_get_dt(&buttons[i]);
      button_event_t evt = {.button_id = i,
                            .type =
                                val ? BUTTON_EVENT_PRESS : BUTTON_EVENT_RELEASE,
                            .timestamp_ms = now};
      k_msgq_put(&button_msgq, &evt, K_NO_WAIT);
    }
  }
}

int buttons_init(void) {
  int ret;

  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (!gpio_is_ready_dt(&buttons[i])) {
      LOG_ERR("Button %d device %s not ready", i, buttons[i].port->name);
      return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT | GPIO_PULL_UP);
    if (ret != 0) {
      LOG_ERR("Failed to configure button %d", i);
      return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_EDGE_FALLING);
    if (ret != 0) {
      LOG_ERR("Failed to set interrupt for button %d", i);
      return ret;
    }

    gpio_init_callback(&button_cb_data[i], button_isr, BIT(buttons[i].pin));
    gpio_add_callback(buttons[i].port, &button_cb_data[i]);

    LOG_INF("Initialized button %d on %s pin %d", i, buttons[i].port->name,
            buttons[i].pin);
  }

  return 0;
}
