#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>

#include "lora_app.h"

LOG_MODULE_REGISTER(lora_app, CONFIG_LOG_DEFAULT_LEVEL);

// Ensure proper alignment for the message queue
K_MSGQ_DEFINE(lora_msgq, sizeof(lora_uplink_msg_t), LORA_MSGQ_SIZE, 4);

// static uint8_t serialize_button_payload(button_payload_t *payload,
//                                         uint8_t *buffer) {
//   uint8_t idx = 0;
//   buffer[idx++] = payload->button_id;

//   // Serialize timestamp (4 bytes, little-endian)
//   buffer[idx++] = (payload->timestamp_ms >> 0) & 0xFF;
//   buffer[idx++] = (payload->timestamp_ms >> 8) & 0xFF;
//   buffer[idx++] = (payload->timestamp_ms >> 16) & 0xFF;
//   buffer[idx++] = (payload->timestamp_ms >> 24) & 0xFF;

//   buffer[idx++] = payload->text_len;
//   for (uint8_t i = 0; i < payload->text_len; i++) {
//     buffer[idx++] = payload->text[i];
//   }

//   return idx; // total length of serialized payload
// }

bool lora_get_event(lora_uplink_msg_t *msg, k_timeout_t timeout) {
  if (msg == NULL) {
    LOG_ERR("lora_get_event: NULL message pointer");
    return false;
  }

  int ret = k_msgq_get(&lora_msgq, msg, timeout);
  if (ret != 0) {
    LOG_ERR("lora_get_event failed: %d", ret);
    return false;
  }
  return true;
}

int lora_put_event(const lora_uplink_msg_t *msg, k_timeout_t timeout) {
  if (msg == NULL) {
    LOG_ERR("lora_put_event: NULL message pointer");
    return -EINVAL;
  }

  if (msg->len > LORA_PAYLOAD_MAX) {
    LOG_ERR("lora_put_event: message too long (%d > %d)", msg->len,
            LORA_PAYLOAD_MAX);
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

  if (hex_data && len > 0) {
    LOG_HEXDUMP_INF(hex_data, len, "Payload: ");
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

  struct lorawan_downlink_cb dl_cb = {.port = LW_RECV_PORT_ANY,
                                      .cb = lora_app_dl_callback};
  lorawan_register_downlink_callback(&dl_cb);

  lorawan_register_dr_changed_callback(lora_app_dr_changed);

  LOG_INF("LoRaWAN stack initialized successfully.");
  return 0;
}
