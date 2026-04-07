#include "power_ctrl.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(power_ctrl, LOG_LEVEL_INF);

#define EN3V3_NODE DT_NODELABEL(en3v3)
#define EN1V8_NODE DT_NODELABEL(en1v8)
#define EN3V3A_NODE DT_NODELABEL(en3v3a)
#define EN3V6_NODE DT_NODELABEL(en3v6)

static const struct gpio_dt_spec power_gpios[POWER_DOMAIN_COUNT] = {
    GPIO_DT_SPEC_GET_OR(EN3V3_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(EN1V8_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(EN3V3A_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(EN3V6_NODE, gpios, {0}),
};

/*
 * Ensure external power rails are enabled early enough for devices that
 * initialize before main() (e.g., the PCF8523 RTC driver).
 *
 * The RTC driver may probe the I2C device during POST_KERNEL init; if the
 * rail is off at that point, the probe fails and the device will remain
 * "not ready" for the rest of the boot.
 */
static int power_ctrl_boot_enable(void) {
  for (int i = 0; i < POWER_DOMAIN_COUNT; i++) {
    if (!gpio_is_ready_dt(&power_gpios[i])) {
      /* If a domain isn't present/ready, just skip it. */
      continue;
    }

    /* Configure and drive high immediately. */
    (void)gpio_pin_configure_dt(&power_gpios[i], GPIO_OUTPUT_ACTIVE);
    (void)gpio_pin_set_dt(&power_gpios[i], 1);
  }

  return 0;
}

SYS_INIT(power_ctrl_boot_enable, EARLY, 0);

int power_ctrl_init(void) {
  int ret;

  for (int i = 0; i < POWER_DOMAIN_COUNT; i++) {
    if (!gpio_is_ready_dt(&power_gpios[i])) {
      LOG_ERR("Power GPIO %d device %s not ready", i,
              power_gpios[i].port->name);
      return -ENODEV;
    }

    /* Keep domains on by default; callers can disable explicitly if needed. */
    ret = gpio_pin_configure_dt(&power_gpios[i], GPIO_OUTPUT_ACTIVE);
    if (ret != 0) {
      LOG_ERR("Failed to configure power GPIO %d", i);
      return ret;
    }

    LOG_INF("Initialized power GPIO %d on %s pin %d", i,
            power_gpios[i].port->name, power_gpios[i].pin);
  }

  return 0;
}

int power_ctrl_set(enum power_domain domain, bool enable) {
  if (domain < 0 || domain >= POWER_DOMAIN_COUNT) {
    return -EINVAL;
  }

  return gpio_pin_set_dt(&power_gpios[domain], enable);
}

int power_ctrl_toggle(enum power_domain domain) {
  if (domain < 0 || domain >= POWER_DOMAIN_COUNT) {
    return -EINVAL;
  }

  return gpio_pin_toggle_dt(&power_gpios[domain]);
}

/* LoRa radio power domain stabilization delay (ms) */
#define LORA_POWER_STABILIZATION_MS 10

int power_ctrl_lora_power_up(void) {
  int ret = power_ctrl_set(POWER_EN_3V6, true);
  if (ret != 0) {
    LOG_ERR("Failed to power up LoRa domain: %d", ret);
    return ret;
  }
  
  /* Allow power rail to stabilize before using radio */
  k_msleep(LORA_POWER_STABILIZATION_MS);
  LOG_DBG("LoRa power domain (3V6) powered up");
  return 0;
}

int power_ctrl_lora_power_down(void) {
  int ret = power_ctrl_set(POWER_EN_3V6, false);
  if (ret != 0) {
    LOG_ERR("Failed to power down LoRa domain: %d", ret);
    return ret;
  }
  
  LOG_DBG("LoRa power domain (3V6) powered down");
  return 0;
}
