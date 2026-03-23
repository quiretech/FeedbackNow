#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/random/random.h>

#include "log_fmt.h"
#include "lora_app.h"
#include "smf_system_mode.h"
#include "time_sync.h"

LOG_MODULE_REGISTER(lora_app, CONFIG_LOG_DEFAULT_LEVEL);

// Ensure proper alignment for the message queue
K_MSGQ_DEFINE(lora_msgq, sizeof(lora_uplink_msg_t), LORA_MSGQ_SIZE, 4);

/* Command queue: SMF (or bootstrap) posts JOIN/TIME_SYNC; LoRa thread consumes
 * only. */
#define LORA_CMDQ_SIZE 4
K_MSGQ_DEFINE(lora_cmdq, sizeof(uint8_t), LORA_CMDQ_SIZE, 4);

/* Join status tracking */
atomic_t lora_joined_flag = ATOMIC_INIT(0);
K_SEM_DEFINE(lora_join_sem, 0, 1);
extern struct k_sem lora_ready_sem;

/* One-shot guard for DR-based time-sync retry per join lifecycle. */
static atomic_t dr_time_sync_retry_requested = ATOMIC_INIT(0);
static atomic_t latest_dr_seen = ATOMIC_INIT(-1);

int lora_cmd_put(uint8_t cmd) {
  if (cmd >= LORA_CMD_COUNT) {
    return -EINVAL;
  }
  int ret = k_msgq_put(&lora_cmdq, &cmd, K_NO_WAIT);
  if (ret != 0) {
    LOG_WRN("lora_cmd_put: queue full, cmd=%u", cmd);
  }
  return ret;
}

void lora_request_join(void) { (void)lora_cmd_put(LORA_CMD_JOIN); }

void lora_request_time_sync(void) { (void)lora_cmd_put(LORA_CMD_TIME_SYNC); }

void lora_request_enable_adr(void) {
  (void)lora_cmd_put(LORA_CMD_ENABLE_ADR);
}

void lora_request_link_check(bool force_request) {
  (void)lora_cmd_put(force_request ? LORA_CMD_LINK_CHECK_FORCE
                                   : LORA_CMD_LINK_CHECK);
}

void lora_reset_dr_time_sync_retry(void) {
  atomic_set(&dr_time_sync_retry_requested, 0);
}

void lora_on_join_success(void) {
  lora_reset_dr_time_sync_retry();
  /* Time sync is requested by smf_joined_work after all counter-syncs are sent */
}

int lora_wait_until_ready(k_timeout_t timeout) {
  return k_sem_take(&lora_ready_sem, timeout);
}

bool lora_get_cmd(uint8_t *cmd_out, k_timeout_t timeout) {
  if (cmd_out == NULL) {
    return false;
  }
  int ret = k_msgq_get(&lora_cmdq, cmd_out, timeout);
  if (ret != 0) {
    return false;
  }
  return true;
}

/* Battery level callback for LoRaWAN MAC commands (0=external power, 1..254
 * level, 255=unknown). We return a random 1..254 value to see if the LNS
 * consumes it.
 */
static uint8_t lora_battery_level_cb(void) {
  uint32_t r = 0;
  sys_rand_get(&r, sizeof(r));
  return (uint8_t)(1 + (r % 254U)); /* 1..254 */
}

bool lora_get_event(lora_uplink_msg_t *msg, k_timeout_t timeout) {
  if (msg == NULL) {
    LOG_ERR("lora_get_event: NULL message pointer");
    return false;
  }

  int ret = k_msgq_get(&lora_msgq, msg, timeout);
  if (ret != 0) {
    if (ret != -EAGAIN && ret != -ENOMSG) {
      LOG_ERR("lora_get_event failed: %d", ret);
    }
    return false;
  }
  return true;
}

int lora_put_event(const lora_uplink_msg_t *msg, k_timeout_t timeout) {
  if (msg == NULL) {
    LOG_ERR("lora_put_event: NULL message pointer");
    return -EINVAL;
  }

  /* Reject messages if not joined to network */
  if (!lora_is_joined()) {
    LOG_WRN("lora_put_event: Cannot queue message - not joined to network");
    return -ENOTCONN;
  }

  if (msg->len > LORA_MAX_PAYLOAD_SIZE) {
    LOG_ERR("lora_put_event: message too long (%d > %d)", msg->len,
            LORA_MAX_PAYLOAD_SIZE);
    return -EINVAL;
  }

  int ret = k_msgq_put(&lora_msgq, msg, timeout);
  if (ret != 0) {
    LOG_ERR("LoRa queue overflow, dropping packet: %d", ret);
  }
  return ret;
}

/**
 * @brief Downlink callback
 */
void lora_app_dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr,
                          uint8_t len, const uint8_t *hex_data) {
  LOG_INF("Port %d, Pending %d, RSSI %ddB, SNR %ddBm, Time %d", port,
          flags & LORAWAN_DATA_PENDING, rssi, snr,
          !!(flags & LORAWAN_TIME_UPDATED));

  if (flags & LORAWAN_TIME_UPDATED) {
    LOG_SECTION_INF(
        "LoRaWAN time updated by network (DeviceTimeAns / clock sync)");
    time_sync_on_lorawan_time_updated();
  }

  if (!hex_data || len == 0) {
    return;
  }

  LOG_HEXDUMP_INF(hex_data, len, "Payload:");

  /* Post to SMF for command dispatch (Phase 2) */
  if (smf_post_downlink(port, len, hex_data) != 0) {
    LOG_WRN("smf_post_downlink failed");
  }
}

/**
 * @brief Data rate change callback
 */
void lora_app_dr_changed(enum lorawan_datarate dr) {
  uint8_t unused, max_size;

  lorawan_get_payload_sizes(&unused, &max_size);
  LOG_INF("New Datarate: DR_%d, Max Payload %d", dr, max_size);
  atomic_set(&latest_dr_seen, (atomic_val_t)dr);
}

/**
 * @brief Initialize LoRaWAN stack and register callbacks
 */
int lora_app_init(void) {
  int ret;
  const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
  if (!device_is_ready(lora_dev)) {
    LOG_ERR("%s: device not ready.", lora_dev->name);
    return -ENODEV;
  }
  LOG_INF("%s: device ready.", lora_dev->name);

  ret = lorawan_start();
  if (ret < 0) {
    LOG_ERR("lorawan_start failed: %d", ret);
    return ret;
  }

  static struct lorawan_downlink_cb dl_cb = {.port = LW_RECV_PORT_ANY,
                                             .cb = lora_app_dl_callback};
  lorawan_register_downlink_callback(&dl_cb);

  lorawan_register_dr_changed_callback(lora_app_dr_changed);

  /* Register battery level callback (randomized) */
  lorawan_register_battery_level_callback(lora_battery_level_cb);

  /* ADR disabled at init; re-enabled after time sync so DR sticks for
   * DeviceTimeAns (DR0 downlinks often fail). See lora_request_enable_adr(). */
  lorawan_enable_adr(false);
  LOG_INF("Adaptive Data Rate (ADR) disabled at init, enabled after time sync");

  LOG_INF("LoRaWAN stack initialized successfully.");
#if defined(CONFIG_LORAMAC_REGION_EU868) && defined(CONFIG_LORAMAC_REGION_US915)
  LOG_INF("LoRaWAN regions compiled: EU868 + US915");
#elif defined(CONFIG_LORAMAC_REGION_EU868)
  LOG_INF("LoRaWAN region compiled: EU868");
#elif defined(CONFIG_LORAMAC_REGION_US915)
  LOG_INF("LoRaWAN region compiled: US915");
#endif
  return 0;
}
