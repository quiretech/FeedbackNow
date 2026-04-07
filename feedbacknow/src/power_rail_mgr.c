#include "power_rail_mgr.h"

#include "power_ctrl.h"
#include "sys_config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(power_rail_mgr, LOG_LEVEL_INF);

static struct k_mutex rail_lock;
static struct k_condvar rail_cv;
static bool rail_mgr_initialized;

static uint16_t on_holds[POWER_RAIL_CLIENT_COUNT];
static uint16_t off_holds[POWER_RAIL_CLIENT_COUNT];
static uint16_t total_on_holds;
static uint16_t total_off_holds;

static bool hw_3v3a_state; /* true=ON, false=OFF (best-effort tracked) */
static uint16_t v6_on_holds[POWER_RAIL_CLIENT_COUNT];
static uint16_t total_v6_on_holds;
static bool hw_3v6_state; /* true=ON, false=OFF (best-effort tracked) */

static const char *client_name(enum power_rail_client c) {
  switch (c) {
  case POWER_RAIL_CLIENT_LORA:
    return "LORA";
  case POWER_RAIL_CLIENT_NFC:
    return "NFC";
  case POWER_RAIL_CLIENT_EPD:
    return "EPD";
  case POWER_RAIL_CLIENT_BOOT:
    return "BOOT";
  case POWER_RAIL_CLIENT_SD:
    return "SD";
  default:
    return "UNKNOWN";
  }
}

static void dump_state_locked(const char *reason) {
  LOG_INF("Rail state%s%s: 3V3A(hw=%d on=%u off=%u) 3V6(hw=%d on=%u)",
          reason ? " (" : "", reason ? reason : "", hw_3v3a_state, total_on_holds,
          total_off_holds, hw_3v6_state, total_v6_on_holds);

  for (int i = 0; i < POWER_RAIL_CLIENT_COUNT; i++) {
    if (on_holds[i] || off_holds[i] || v6_on_holds[i]) {
      LOG_INF("  client=%s: 3V3A(on=%u off=%u) 3V6(on=%u)", client_name(i),
              on_holds[i], off_holds[i], v6_on_holds[i]);
    }
  }
}

void power_rail_mgr_dump_state(void) {
  if (!rail_mgr_initialized) {
    LOG_WRN("Rail manager not initialized");
    return;
  }

  k_mutex_lock(&rail_lock, K_FOREVER);
  dump_state_locked("dump");
  k_mutex_unlock(&rail_lock);
}

static void apply_hw_3v3a_locked(bool enable) {
  if (hw_3v3a_state == enable) {
    return;
  }

  int ret = power_ctrl_set(POWER_EN_3V3A, enable);
  if (ret != 0) {
    LOG_ERR("Failed to set 3V3A=%d: %d", enable, ret);
    /* Keep going; we still update internal state to avoid deadlocks. */
  }

  hw_3v3a_state = enable;

  if (enable) {
    k_sleep(K_MSEC(POWER_RAIL_3V3A_ON_DELAY_MS));
  } else {
    k_sleep(K_MSEC(POWER_RAIL_3V3A_OFF_DELAY_MS));
  }
}

static void apply_hw_3v6_locked(bool enable) {
  if (hw_3v6_state == enable) {
    return;
  }

  int ret = power_ctrl_set(POWER_EN_3V6, enable);
  if (ret != 0) {
    LOG_ERR("Failed to set 3V6=%d: %d", enable, ret);
    /* Keep going; we still update internal state to avoid deadlocks. */
  }

  hw_3v6_state = enable;

  if (enable) {
    k_sleep(K_MSEC(POWER_RAIL_3V6_ON_DELAY_MS));
  } else {
    k_sleep(K_MSEC(POWER_RAIL_3V6_OFF_DELAY_MS));
  }
}

static void reconcile_3v3a_locked(void) {
  /* OFF wins over ON. If nobody holds either, default OFF (LoRa-friendly). */
  if (total_off_holds > 0) {
    apply_hw_3v3a_locked(false);
    return;
  }

  if (total_on_holds > 0) {
    apply_hw_3v3a_locked(true);
    return;
  }

  apply_hw_3v3a_locked(false);
}

static void reconcile_3v6_locked(void) {
  /* 3V6 is intended to be enabled only when explicitly requested. */
  apply_hw_3v6_locked(total_v6_on_holds > 0);
}

int power_rail_mgr_init(void) {
  k_mutex_init(&rail_lock);
  k_condvar_init(&rail_cv);

  for (int i = 0; i < POWER_RAIL_CLIENT_COUNT; i++) {
    on_holds[i] = 0;
    off_holds[i] = 0;
    v6_on_holds[i] = 0;
  }
  total_on_holds = 0;
  total_off_holds = 0;
  total_v6_on_holds = 0;

  /* Assume boot default is OFF for LoRa compatibility. */
  hw_3v3a_state = false;
  (void)power_ctrl_set(POWER_EN_3V3A, false);

  /* Default 3V6 OFF; NFC will request it ON as needed. */
  hw_3v6_state = false;
  (void)power_ctrl_set(POWER_EN_3V6, false);

  rail_mgr_initialized = true;
  LOG_INF("Power rail manager initialized");
  return 0;
}

int power_rail_mgr_require_3v6_on(enum power_rail_client client,
                                  k_timeout_t timeout) {
  (void)timeout;

  if (!rail_mgr_initialized) {
    LOG_WRN("Rail manager not initialized; forcing 3V6 ON for %s",
            client_name(client));
    return power_ctrl_set(POWER_EN_3V6, true);
  }

  k_mutex_lock(&rail_lock, K_FOREVER);

  v6_on_holds[client]++;
  total_v6_on_holds++;
  LOG_DBG("3V6 ON hold++: %s (total_on=%u)", client_name(client),
          total_v6_on_holds);

  reconcile_3v6_locked();
  k_condvar_broadcast(&rail_cv);
  k_mutex_unlock(&rail_lock);
  return 0;
}

void power_rail_mgr_release_3v6_on(enum power_rail_client client) {
  if (!rail_mgr_initialized) {
    return;
  }

  k_mutex_lock(&rail_lock, K_FOREVER);

  if (v6_on_holds[client] == 0) {
    LOG_WRN("3V6 ON release without hold: %s", client_name(client));
    k_mutex_unlock(&rail_lock);
    return;
  }

  v6_on_holds[client]--;
  total_v6_on_holds--;
  LOG_DBG("3V6 ON hold--: %s (total_on=%u)", client_name(client),
          total_v6_on_holds);

  reconcile_3v6_locked();
  k_condvar_broadcast(&rail_cv);
  k_mutex_unlock(&rail_lock);
}

int power_rail_mgr_require_3v3a_on(enum power_rail_client client,
                                   k_timeout_t timeout) {
  if (!rail_mgr_initialized) {
    LOG_WRN("Rail manager not initialized; forcing 3V3A ON for %s",
            client_name(client));
    return power_ctrl_set(POWER_EN_3V3A, true);
  }

  k_mutex_lock(&rail_lock, K_FOREVER);

  while (total_off_holds > 0) {
    LOG_WRN("3V3A ON requested by %s but OFF holds present; waiting...",
            client_name(client));
    dump_state_locked("wait_on");
    int ret = k_condvar_wait(&rail_cv, &rail_lock, timeout);
    if (ret != 0) {
      k_mutex_unlock(&rail_lock);
      return -EAGAIN;
    }
  }

  on_holds[client]++;
  total_on_holds++;
  LOG_DBG("3V3A ON hold++: %s (total_on=%u)", client_name(client),
          total_on_holds);

  reconcile_3v3a_locked();
  k_condvar_broadcast(&rail_cv);
  k_mutex_unlock(&rail_lock);
  return 0;
}

void power_rail_mgr_release_3v3a_on(enum power_rail_client client) {
  if (!rail_mgr_initialized) {
    return;
  }

  k_mutex_lock(&rail_lock, K_FOREVER);

  if (on_holds[client] == 0) {
    LOG_WRN("3V3A ON release without hold: %s", client_name(client));
    k_mutex_unlock(&rail_lock);
    return;
  }

  on_holds[client]--;
  total_on_holds--;
  LOG_DBG("3V3A ON hold--: %s (total_on=%u)", client_name(client),
          total_on_holds);

  reconcile_3v3a_locked();
  k_condvar_broadcast(&rail_cv);
  k_mutex_unlock(&rail_lock);
}

int power_rail_mgr_require_3v3a_off(enum power_rail_client client,
                                    k_timeout_t timeout) {
  if (!rail_mgr_initialized) {
    LOG_WRN("Rail manager not initialized; forcing 3V3A OFF for %s",
            client_name(client));
    return power_ctrl_set(POWER_EN_3V3A, false);
  }

  k_mutex_lock(&rail_lock, K_FOREVER);

  while (total_on_holds > 0) {
    LOG_WRN("3V3A OFF requested by %s but ON holds present; waiting...",
            client_name(client));
    dump_state_locked("wait_off");
    int ret = k_condvar_wait(&rail_cv, &rail_lock, timeout);
    if (ret != 0) {
      k_mutex_unlock(&rail_lock);
      return -EAGAIN;
    }
  }

  off_holds[client]++;
  total_off_holds++;
  LOG_DBG("3V3A OFF hold++: %s (total_off=%u)", client_name(client),
          total_off_holds);

  reconcile_3v3a_locked();
  k_condvar_broadcast(&rail_cv);
  k_mutex_unlock(&rail_lock);
  return 0;
}

void power_rail_mgr_release_3v3a_off(enum power_rail_client client) {
  if (!rail_mgr_initialized) {
    return;
  }

  k_mutex_lock(&rail_lock, K_FOREVER);

  if (off_holds[client] == 0) {
    LOG_WRN("3V3A OFF release without hold: %s", client_name(client));
    k_mutex_unlock(&rail_lock);
    return;
  }

  off_holds[client]--;
  total_off_holds--;
  LOG_DBG("3V3A OFF hold--: %s (total_off=%u)", client_name(client),
          total_off_holds);

  reconcile_3v3a_locked();
  k_condvar_broadcast(&rail_cv);
  k_mutex_unlock(&rail_lock);
}
