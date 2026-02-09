#ifndef POWER_CTRL_H
#define POWER_CTRL_H

#include <stdbool.h>

enum power_domain {
  POWER_EN_3V3 = 0,
  POWER_EN_1V8,
  POWER_EN_3V3A,
  POWER_EN_3V6,
  POWER_DOMAIN_COUNT,
};

int power_ctrl_init(void);
int power_ctrl_set(enum power_domain domain, bool enable);
int power_ctrl_toggle(enum power_domain domain);

/**
 * @brief Power up LoRa radio domain (3V6) with stabilization delay
 * @return 0 on success, negative errno on failure
 */
int power_ctrl_lora_power_up(void);

/**
 * @brief Power down LoRa radio domain (3V6)
 * @return 0 on success, negative errno on failure
 */
int power_ctrl_lora_power_down(void);

#endif /* POWER_CTRL_H */
