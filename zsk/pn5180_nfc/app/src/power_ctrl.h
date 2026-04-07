#ifndef POWER_CTRL_H
#define POWER_CTRL_H

#include <stdbool.h>

enum power_domain {
  POWER_EN_3V3 = 0,
  POWER_EN_1V8,
  POWER_EN_3V3A,
  POWER_EN_3V6,
  POWER_LR_RESET,
  POWER_LR_CS,
  POWER_DOMAIN_COUNT,
};

int power_ctrl_init(void);
int power_ctrl_set(enum power_domain domain, bool enable);
int power_ctrl_toggle(enum power_domain domain);

#endif /* POWER_CTRL_H */
