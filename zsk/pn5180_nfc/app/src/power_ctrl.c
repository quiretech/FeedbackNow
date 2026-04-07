#include "power_ctrl.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(power_ctrl, LOG_LEVEL_INF);

#define EN3V3_NODE DT_NODELABEL(en3v3)
#define EN1V8_NODE DT_NODELABEL(en1v8)
#define EN3V3A_NODE DT_NODELABEL(en3v3a)
#define EN3V6_NODE DT_NODELABEL(en3v6)
#define LR_RESET_NODE DT_NODELABEL(lrreset)
#define LR_CS_NODE DT_NODELABEL(lrcs)

static const struct gpio_dt_spec power_gpios[POWER_DOMAIN_COUNT] = {
    GPIO_DT_SPEC_GET_OR(EN3V3_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(EN1V8_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(EN3V3A_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(EN3V6_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(LR_RESET_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(LR_CS_NODE, gpios, {0}),
};

int power_ctrl_init(void) {
  int ret;

  for (int i = 0; i < POWER_DOMAIN_COUNT; i++) {
    if (!gpio_is_ready_dt(&power_gpios[i])) {
      LOG_ERR("Power GPIO %d device %s not ready", i,
              power_gpios[i].port->name);
      return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&power_gpios[i], GPIO_OUTPUT_INACTIVE);
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
