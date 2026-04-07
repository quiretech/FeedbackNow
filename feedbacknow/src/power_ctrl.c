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

static const struct gpio_dt_spec power_gpios[POWER_DOMAIN_COUNT] = {
    GPIO_DT_SPEC_GET_OR(EN3V3_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(EN1V8_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(EN3V3A_NODE, gpios, {0}),
    GPIO_DT_SPEC_GET_OR(EN3V6_NODE, gpios, {0}),
};

static bool power_ctrl_initialized;

int power_ctrl_init(void) {
  int ret;

  if (power_ctrl_initialized) {
    return 0;
  }

  /* Boot defaults chosen to avoid rail glitches during early driver init.
   * We keep core rails ON, and allow 3V3A ON during device initialization
   * (EPD/NFC may be probed by Zephyr before main()). The rail arbiter will
   * later enforce the LoRa policy (3V3A OFF during join/TX/RX windows).
   */
  static const bool boot_default[POWER_DOMAIN_COUNT] = {
      [POWER_EN_3V3] = true,
      [POWER_EN_1V8] = true,
      [POWER_EN_3V3A] = true,
      [POWER_EN_3V6] = false,
  };

  for (int i = 0; i < POWER_DOMAIN_COUNT; i++) {
    if (!gpio_is_ready_dt(&power_gpios[i])) {
      LOG_ERR("Power GPIO %d device %s not ready", i,
              power_gpios[i].port->name);
      return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&power_gpios[i], boot_default[i]
                                                     ? GPIO_OUTPUT_ACTIVE
                                                     : GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
      LOG_ERR("Failed to configure power GPIO %d", i);
      return ret;
    }

    LOG_INF("Initialized power GPIO %d on %s pin %d (default=%s)", i,
            power_gpios[i].port->name, power_gpios[i].pin,
            boot_default[i] ? "ON" : "OFF");
  }

  power_ctrl_initialized = true;
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

/* Ensure enable pins are configured before other POST_KERNEL device init runs.
 */
static int power_ctrl_sys_init(void) { return power_ctrl_init(); }
SYS_INIT(power_ctrl_sys_init, POST_KERNEL, 0);
