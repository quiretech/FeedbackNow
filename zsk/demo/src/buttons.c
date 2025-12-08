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

/* Debouncing configuration */
#define DEBOUNCE_DELAY_MS 20      // Wait this long before confirming press
#define MIN_PRESS_INTERVAL_MS 150 // Minimum time between valid presses
#define REED_SWITCH_BUTTON_ID 6   // Button 6 is the reed switch

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

/* Debouncing state */
static int64_t button_last_valid_press[NUM_BUTTONS] = {0};
static volatile uint8_t pending_buttons =
    0; // Bitmask of buttons waiting for confirmation

/* Debounce work handler - runs after delay to confirm press */
static void debounce_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(debounce_work, debounce_work_handler);

/*Button ISR*/
static void button_isr(const struct device *dev, struct gpio_callback *cb,
                       uint32_t pins);

bool buttons_get_event(button_event_t *event, k_timeout_t timeout) {
  return k_msgq_get(&button_msgq, event, timeout) == 0;
}

/* Work handler - called after debounce delay to confirm button is still pressed
 */
static void debounce_work_handler(struct k_work *work) {
  int64_t now = k_uptime_get();

  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (pending_buttons & BIT(i)) {
      // Clear pending flag
      pending_buttons &= ~BIT(i);

      // CHECK: Is button STILL pressed? (pin should be LOW = 0 for active-low)
      int pin_state = gpio_pin_get_dt(&buttons[i]);

      // gpio_pin_get_dt returns logical state (1 = pressed if ACTIVE_LOW in DT)
      // If your buttons don't have ACTIVE_LOW flag, change pin_state == 1 to
      // pin_state == 0
      if (pin_state == 1) { // Button is confirmed pressed
        // Check minimum interval between presses
        if (now - button_last_valid_press[i] >= MIN_PRESS_INTERVAL_MS) {
          button_last_valid_press[i] = now;

          // Send confirmed press event
          button_event_t evt = {
              .button_id = i, .type = BUTTON_EVENT_PRESS, .timestamp_ms = now};
          k_msgq_put(&button_msgq, &evt, K_NO_WAIT);
          LOG_INF("Button %d CONFIRMED (pin=%d)", i, pin_state);
        }
      } else {
        // Button was released before debounce completed - it was noise!
        LOG_INF("Button %d REJECTED as noise (pin=%d)", i, pin_state);
      }
    }
  }
}

/* Interrupt handler - just schedules debounce work */
static void button_isr(const struct device *dev, struct gpio_callback *cb,
                       uint32_t pins) {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (pins & BIT(buttons[i].pin)) {
      // Mark button as pending confirmation
      pending_buttons |= BIT(i);
    }
  }

  // Schedule work to confirm press after debounce delay
  // If already scheduled, this extends the deadline
  k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_DELAY_MS));
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
