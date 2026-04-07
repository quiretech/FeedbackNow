#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/random/random.h>

#include "log_fmt.h"
#include "lora_app.h"

LOG_MODULE_REGISTER(lora_app, CONFIG_LOG_DEFAULT_LEVEL);

// Ensure proper alignment for the message queue
K_MSGQ_DEFINE(lora_msgq, sizeof(lora_uplink_msg_t), LORA_MSGQ_SIZE, 4);

/* Join status tracking */
atomic_t lora_joined_flag = ATOMIC_INIT(0);
K_SEM_DEFINE(lora_join_sem, 0, 1);

/* Battery level callback for LoRaWAN MAC commands (0=external power, 1..254
 * level, 255=unknown). We return a random 1..254 value to see if the LNS
 * consumes it.
 */
static uint8_t lora_battery_level_cb(void) {
  uint32_t r = 0;
  sys_rand_get(&r, sizeof(r));
  return (uint8_t)(1 + (r % 254U)); /* 1..254 */
}

int lora_wait_for_join(k_timeout_t timeout) {
  /* If already joined, return immediately */
  if (lora_is_joined()) {
    return 0;
  }

  /* Wait for join semaphore */
  int ret = k_sem_take(&lora_join_sem, timeout);
  if (ret == 0) {
    /* Give back the semaphore so other waiters can also proceed */
    k_sem_give(&lora_join_sem);
  }
  return ret;
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
  }

  if (!hex_data || len == 0) {
    return;
  }

  LOG_HEXDUMP_INF(hex_data, len, "Payload:");
}

/**
 * @brief Data rate change callback
 */
void lora_app_dr_changed(enum lorawan_datarate dr) {
  uint8_t unused, max_size;

  lorawan_get_payload_sizes(&unused, &max_size);
  LOG_INF("New Datarate: DR_%d, Max Payload %d", dr, max_size);
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

  // Enable ADR so network manages DR dynamically
  lorawan_enable_adr(true);
  LOG_INF("Adaptive Data Rate (ADR) enabled");

  LOG_INF("LoRaWAN stack initialized successfully.");
  return 0;
}
