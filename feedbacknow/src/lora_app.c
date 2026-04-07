#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>

extern struct k_msgq lora_downlink_msgq;
#include "lora_app.h"
#include "sdcard_logger.h"

LOG_MODULE_REGISTER(lora_app, CONFIG_LOG_DEFAULT_LEVEL);

// Ensure proper alignment for the message queue
K_MSGQ_DEFINE(lora_msgq, sizeof(lora_uplink_msg_t), LORA_MSGQ_SIZE, 4);

bool lora_get_event(lora_uplink_msg_t *msg, k_timeout_t timeout) {
  if (msg == NULL) {
    LOG_ERR("lora_get_event: NULL message pointer");
    return false;
  }

  int ret = k_msgq_get(&lora_msgq, msg, timeout);
  if (ret != 0) {
    /* -EAGAIN is normal when timeout expires with no message */
    if (ret != -EAGAIN) {
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

  if (!hex_data || len == 0) {
    return;
  }

  LOG_HEXDUMP_INF(hex_data, len, "Payload:");

  /* Persist full payload (best-effort) in a safe worker context */
  if (sdcard_logger_is_ready()) {
    int ret =
        sdcard_logger_submit_downlink(port, flags, rssi, snr, hex_data, len);
    if (ret != 0) {
      LOG_WRN("SD submit failed: %d", ret);
    }
  }

  /*
   * Convert binary bytes -> hex string, but ALWAYS enqueue a fixed-size,
   * NUL-terminated message. The msgq item size is 64 bytes (see src/main.c).
   * If the downlink payload is long, the hex string would otherwise be
   * truncated without the terminator, and consumers calling strlen() would
   * read past the buffer (memory corruption).
   */
  char hex_str[64];
  static const char hex[] = "0123456789ABCDEF";
  const uint8_t max_bytes = (uint8_t)((sizeof(hex_str) - 1) / 2);
  uint8_t n = len;
  if (n > max_bytes) {
    n = max_bytes;
    LOG_WRN("Downlink too long (%uB); truncating to %uB for UI queue", len, n);
  }
  for (uint8_t i = 0; i < n; i++) {
    hex_str[i * 2] = hex[(hex_data[i] >> 4) & 0x0F];
    hex_str[i * 2 + 1] = hex[hex_data[i] & 0x0F];
  }
  hex_str[n * 2] = '\0';

  // Enqueue the hex string for decoding/display
  if (k_msgq_put(&lora_downlink_msgq, hex_str, K_NO_WAIT) != 0) {
    LOG_WRN("Downlink message queue full, dropping payload");
  }
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

  // **Enable ADR** so network manages DR dynamically
  lorawan_enable_adr(true);
  LOG_INF("Adaptive Data Rate (ADR) enabled");

  LOG_INF("LoRaWAN stack initialized successfully.");
  return 0;
}
