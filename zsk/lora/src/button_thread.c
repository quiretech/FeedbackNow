#include "button_thread.h"

#include "buttons.h"
#include "lora_app.h"
#include "payload_gen.h"
#include "rtc.h"
#include "sys_config.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button_uplink, CONFIG_LOG_DEFAULT_LEVEL);

/* Map driver button index (0..NUM_BUTTONS-1) -> payload button_id */
static const uint8_t button_id_map[NUM_BUTTONS] = {0, 1, 2, 3, 4, 5, 6};

/* Status LED (blink 1s on button press) */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led0 =
    GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, {0});
static bool led0_ok;
static struct k_work led0_off_work;
static struct k_timer led0_off_timer;

static void led0_off_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  if (!led0_ok) {
    return;
  }
  (void)gpio_pin_set_dt(&led0, 0);
}

static void led0_off_timer_handler(struct k_timer *timer) {
  ARG_UNUSED(timer);
  k_work_submit(&led0_off_work);
}

static void button_uplink_thread_fn(void *a, void *b, void *c) {
  button_event_t btn_evt;
  uint32_t last_press_ms[NUM_BUTTONS] = {0};

  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  LOG_INF("=== BUTTON UPLINK THREAD ENTRY ===");

  /* Initialize LED GPIO and make sure it's OFF by default */
  led0_ok = (led0.port != NULL) && device_is_ready(led0.port);
  if (!led0_ok) {
    LOG_WRN("LED0 not ready; blink disabled");
  } else {
    int ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
      led0_ok = false;
      LOG_WRN("LED0 configure failed (%d); blink disabled", ret);
    } else {
      k_work_init(&led0_off_work, led0_off_work_handler);
      k_timer_init(&led0_off_timer, led0_off_timer_handler, NULL);
    }
  }

  while (1) {
    (void)buttons_get_event(&btn_evt, K_FOREVER);

    if (btn_evt.button_id >= NUM_BUTTONS) {
      continue;
    }

    if (btn_evt.type != BUTTON_EVENT_PRESS) {
      continue; /* demo cares only about presses */
    }

    /* Simple debounce in thread context */
    uint32_t now_ms = (uint32_t)k_uptime_get_32();
    if ((now_ms - last_press_ms[btn_evt.button_id]) < BUTTON_DEBOUNCE_MS) {
      continue;
    }
    last_press_ms[btn_evt.button_id] = now_ms;

    /* Blink LED for 1 second on press */
    if (led0_ok) {
      (void)gpio_pin_set_dt(&led0, 1);
      k_timer_start(&led0_off_timer, K_SECONDS(1), K_NO_WAIT);
    }

    uint32_t epoch_s = 0;
    int ret = rtc_get_epoch_seconds(&epoch_s);
    if (ret != 0) {
      /* Fallback: still send something monotonic if RTC isn't available */
      epoch_s = (uint32_t)(k_uptime_get() / 1000U);
      LOG_WRN("RTC read failed (%d); using uptime seconds=%u", ret, epoch_s);
    }

    uint8_t payload[PAYLOAD_LEN_BYTES] = {0};
    uint8_t payload_button_id = button_id_map[btn_evt.button_id];

    uint32_t new_counter = 0;
    ret = payload_gen_build_button(payload_button_id, epoch_s, payload,
                                   &new_counter);
    if (ret != 0) {
      LOG_ERR("Failed to build button payload: %d", ret);
      continue;
    }

    lora_uplink_msg_t msg = {0};
    msg.port = FPORT_BUTTON;
    msg.confirmed = false;
    msg.len = PAYLOAD_LEN_BYTES;
    memcpy(msg.data, payload, PAYLOAD_LEN_BYTES);

    ret = lora_put_event(&msg, K_NO_WAIT);
    if (ret == -ENOTCONN) {
      LOG_WRN("Not joined yet; dropped button press id=%u", payload_button_id);
    } else if (ret != 0) {
      LOG_ERR("Failed to queue button uplink: %d", ret);
    } else {
      LOG_INF("Queued button uplink: btn=%u ctr=%u ts=%u", payload_button_id,
              new_counter, epoch_s);
    }
  }
}

/* Don't autostart; main.c starts it only in button demo mode */
K_THREAD_DEFINE(button_uplink_thread_id, BUTTON_THREAD_STACK_SIZE,
                button_uplink_thread_fn, NULL, NULL, NULL,
                BUTTON_THREAD_PRIORITY, 0, -1);