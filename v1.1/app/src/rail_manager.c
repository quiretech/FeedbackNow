/**
 * Rail manager: ref-count and keep-alive for 3.3V, 1.8V, 3.3A, 3.6V.
 * 3.3A (and 3.3V when used with 3.3A) has keep-alive after last release.
 */
#include "rail_manager.h"
#include "power_ctrl.h"
#include "rtc.h"
#include "sys_config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rail_manager, CONFIG_LOG_DEFAULT_LEVEL);

#define KEEPALIVE_MS RAIL_MANAGER_3V3A_KEEPALIVE_MS

static struct k_mutex lock;
static int ref_3v3;
static int ref_1v8;
static int ref_3v3a;
static int ref_3v6;

/* When 3.3A ref goes to 0, we delay turning off by KEEPALIVE_MS */
static volatile bool keepalive_pending;

static void set_rail(enum power_domain domain, bool on) {
  int r = power_ctrl_set(domain, on);
  if (r != 0) {
    LOG_ERR("power_ctrl_set(%d, %d) failed: %d", domain, (int)on, r);
  }
}

static void keepalive_expiry(struct k_work *work) {
  ARG_UNUSED(work);
  k_mutex_lock(&lock, K_FOREVER);
  keepalive_pending = false;
  if (ref_3v3a == 0) {
    set_rail(POWER_EN_3V3A, false);
    if (ref_3v3 == 0) {
      set_rail(POWER_EN_3V3, false);
    }
    LOG_DBG("3.3A (and 3.3V if 0) off after keepalive");
  }
  k_mutex_unlock(&lock);
}

/* Static definition so handler is never re-inited; avoids null handler in work queue. */
K_WORK_DELAYABLE_DEFINE(keepalive_work, keepalive_expiry);

int rail_manager_init(void) {
  k_mutex_init(&lock);
  ref_3v3 = 0;
  ref_1v8 = 0;
  ref_3v3a = 0;
  ref_3v6 = 0;
  keepalive_pending = false;
  LOG_INF("Rail manager init (ref-counts 0)");
  return 0;
}

void rail_manager_enter_idle(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (keepalive_pending) {
    (void)k_work_cancel_delayable(&keepalive_work);
    keepalive_pending = false;
  }
  ref_3v3 = 0;
  ref_3v3a = 0;
  ref_3v6 = 0;
  set_rail(POWER_EN_3V3, false);
  set_rail(POWER_EN_3V3A, false);
  set_rail(POWER_EN_3V6, false);
  k_mutex_unlock(&lock);
  LOG_DBG("Enter idle: 3.3V/3.3A/3.6V off");
}

void rail_manager_request_3v3(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (ref_3v3++ == 0) {
    set_rail(POWER_EN_3V3, true);
  }
  k_mutex_unlock(&lock);
}

void rail_manager_release_3v3(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (ref_3v3 > 0) {
    ref_3v3--;
  } else {
    LOG_ERR("rail_manager_release_3v3: ref already 0 (double release?)");
    ref_3v3 = 0; /* Prevent negative */
  }
  if (ref_3v3 == 0) {
    if (ref_3v3a > 0) {
      /* 3.3V tied to 3.3A when 3.3A is held; don't turn off 3.3V here */
    } else {
      set_rail(POWER_EN_3V3, false);
    }
  }
  k_mutex_unlock(&lock);
}

void rail_manager_request_1v8(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (ref_1v8++ == 0) {
    set_rail(POWER_EN_1V8, true);
  }
  k_mutex_unlock(&lock);
}

void rail_manager_release_1v8(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (ref_1v8 > 0) {
    ref_1v8--;
  } else {
    LOG_ERR("rail_manager_release_1v8: ref already 0 (double release?)");
    ref_1v8 = 0;
  }
  if (ref_1v8 == 0) {
    set_rail(POWER_EN_1V8, false);
  }
  k_mutex_unlock(&lock);
}

void rail_manager_request_3v3a(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (keepalive_pending) {
    (void)k_work_cancel_delayable(&keepalive_work);
    keepalive_pending = false;
  }
  ref_3v3a++;
  ref_3v3++; /* tie 3.3V to 3.3A for peripheral rail */
  if (ref_3v3a == 1) {
    set_rail(POWER_EN_3V3A, true);
    rtc_notify_3v3a_enabled();
  }
  if (ref_3v3 == 1) {
    set_rail(POWER_EN_3V3, true);
  }
  k_mutex_unlock(&lock);
}

void rail_manager_release_3v3a(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (ref_3v3a > 0) {
    ref_3v3a--;
  } else {
    LOG_ERR("rail_manager_release_3v3a: ref already 0 (double release?)");
    ref_3v3a = 0; /* Prevent negative */
  }
  if (ref_3v3 > 0) {
    ref_3v3--;
  } else {
    LOG_ERR("rail_manager_release_3v3a: 3v3 ref already 0 (double release?)");
    ref_3v3 = 0; /* Prevent negative */
  }
  if (ref_3v3a == 0) {
    keepalive_pending = true;
    k_mutex_unlock(&lock);
    (void)k_work_schedule(&keepalive_work, K_MSEC(KEEPALIVE_MS));
    return;
  }
  k_mutex_unlock(&lock);
}

void rail_manager_request_3v6(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (ref_3v6++ == 0) {
    set_rail(POWER_EN_3V6, true);
  }
  k_mutex_unlock(&lock);
}

void rail_manager_release_3v6(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (ref_3v6 > 0) {
    ref_3v6--;
  } else {
    LOG_ERR("rail_manager_release_3v6: ref already 0 (double release?)");
    ref_3v6 = 0;
  }
  if (ref_3v6 == 0) {
    set_rail(POWER_EN_3V6, false);
  }
  k_mutex_unlock(&lock);
}

void rail_manager_keepalive_3v3a(void) {
  k_mutex_lock(&lock, K_FOREVER);
  if (ref_3v3a == 0 && keepalive_pending) {
    (void)k_work_cancel_delayable(&keepalive_work);
    keepalive_pending = false;
    ref_3v3a = 1;
    ref_3v3 = 1;
    set_rail(POWER_EN_3V3A, true);
    set_rail(POWER_EN_3V3, true);
    keepalive_pending = true;
    k_mutex_unlock(&lock);
    (void)k_work_schedule(&keepalive_work, K_MSEC(KEEPALIVE_MS));
    return;
  }
  k_mutex_unlock(&lock);
}
