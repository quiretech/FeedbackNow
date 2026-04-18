/**
 * Payload builder for LoRa uplinks (FRD 4.5). Event types in payload_gen.h
 * (e.g. EVT_BUTTON 0x00, EVT_COUNTER_SYNC 0x12, NFC, battery); RTC/epoch from
 * caller.
 */
#include "payload_gen.h"

#include "button_counter_store.h"
#include "sys_config.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(payload_gen, CONFIG_LOG_DEFAULT_LEVEL);

BUILD_ASSERT(PAYLOAD_LEN_BYTES == LORA_MAX_PAYLOAD_SIZE,
             "payload size must match LoRa max payload");

#define BUTTON_ID_MAX (NUM_BUTTONS - 1)

static struct {
  struct k_mutex lock;
} ctx;

static void write_be32(uint8_t *buf, uint32_t v) {
  buf[0] = (uint8_t)(v >> 24);
  buf[1] = (uint8_t)(v >> 16);
  buf[2] = (uint8_t)(v >> 8);
  buf[3] = (uint8_t)(v);
}

void payload_gen_init(void) {
  k_mutex_init(&ctx.lock);
  LOG_INF("payload_gen initialized");
}

int payload_gen_build_button(uint8_t button_id, uint32_t epoch_s,
                             uint8_t *out_buf, uint32_t *out_counter) {
  if (!out_buf) {
    return -EINVAL;
  }
  if (button_id > BUTTON_ID_MAX) {
    return -EINVAL;
  }

  k_mutex_lock(&ctx.lock, K_FOREVER);

  write_be32(out_buf, epoch_s);
  out_buf[4] = EVT_BUTTON;
  out_buf[5] = button_id;

  uint32_t c = 0;
  if (button_counter_store_inc(button_id, &c) != 0) {
    c = 0;
  }
  out_buf[6] = (uint8_t)(c >> 16);
  out_buf[7] = (uint8_t)(c >> 8);
  out_buf[8] = (uint8_t)(c);
  out_buf[9] = 0x00;
  out_buf[10] = 0x00;

  if (out_counter) {
    *out_counter = c;
  }

  k_mutex_unlock(&ctx.lock);
  return 0;
}

int payload_gen_build_counter_sync(uint8_t button_id, uint32_t epoch_s,
                                   uint8_t *out_buf) {
  if (!out_buf || button_id > BUTTON_ID_MAX) {
    return -EINVAL;
  }
  uint32_t c = 0;
  (void)button_counter_store_get(button_id, &c);
  write_be32(out_buf, epoch_s);
  out_buf[4] = EVT_COUNTER_SYNC;
  out_buf[5] = button_id;
  out_buf[6] = (uint8_t)(c >> 16);
  out_buf[7] = (uint8_t)(c >> 8);
  out_buf[8] = (uint8_t)(c);
  out_buf[9] = 0x00;
  out_buf[10] = 0x00;
  return 0;
}

int payload_gen_build_battery_status(uint32_t epoch_s, uint16_t battery_mv,
                                     uint8_t percent, uint8_t flags,
                                     uint8_t *out_buf) {
  if (!out_buf) {
    return -EINVAL;
  }

  k_mutex_lock(&ctx.lock, K_FOREVER);

  /* Timestamp */
  write_be32(out_buf, epoch_s);
  /* Event type */
  out_buf[4] = EVT_BATTERY_STATUS;
  /* Battery millivolts (big-endian) */
  out_buf[5] = (uint8_t)(battery_mv >> 8);
  out_buf[6] = (uint8_t)(battery_mv & 0xFF);
  /* Percent and flags (caller can pass 0 for now) */
  out_buf[7] = percent;
  out_buf[8] = flags;
  /* Reserved */
  out_buf[9] = 0x00;
  out_buf[10] = 0x00;

  k_mutex_unlock(&ctx.lock);
  return 0;
}

int payload_gen_build_nfc_in(uint32_t epoch_s, const uint8_t *data_4,
                             uint8_t *out_buf) {
  if (!out_buf || !data_4) {
    return -EINVAL;
  }
  write_be32(out_buf, epoch_s);
  out_buf[4] = EVT_NFC_IN;
  memcpy(&out_buf[5], data_4, 4);
  out_buf[9] = 0x00;
  out_buf[10] = 0x00;
  return 0;
}

int payload_gen_build_nfc_out(uint32_t epoch_s, const uint8_t *data_4,
                              uint8_t *out_buf) {
  if (!out_buf || !data_4) {
    return -EINVAL;
  }
  write_be32(out_buf, epoch_s);
  out_buf[4] = EVT_NFC_OUT;
  memcpy(&out_buf[5], data_4, 4);
  out_buf[9] = 0x00;
  out_buf[10] = 0x00;
  return 0;
}

int payload_gen_build_nfc_vote(uint32_t epoch_s, uint8_t button_id,
                               const uint8_t *data_4, uint8_t *out_buf) {
  if (!out_buf || !data_4) {
    return -EINVAL;
  }
  write_be32(out_buf, epoch_s);
  out_buf[4] = EVT_NFC_VOTE;
  out_buf[5] = button_id;
  memcpy(&out_buf[6], data_4, 4);
  out_buf[10] = 0x00;
  return 0;
}

int payload_gen_build_device_state_snapshot(uint32_t epoch_s,
                                            uint32_t last_cleaned_epoch,
                                            int16_t tz_offset_minutes,
                                            uint8_t *out_buf) {
  if (!out_buf) {
    return -EINVAL;
  }
  write_be32(out_buf, epoch_s);
  out_buf[4] = EVT_DEVICE_STATE_SNAPSHOT;
  write_be32(&out_buf[5], last_cleaned_epoch);
  uint16_t tz_u = (uint16_t)tz_offset_minutes;
  out_buf[9] = (uint8_t)(tz_u >> 8);
  out_buf[10] = (uint8_t)(tz_u & 0xFF);
  return 0;
}

int payload_gen_build_device_version_info(uint32_t epoch_s, uint8_t *out_buf) {
  if (!out_buf) {
    return -EINVAL;
  }
  memset(out_buf, 0, PAYLOAD_LEN_BYTES);
  write_be32(out_buf, epoch_s);
  out_buf[4] = EVT_DEVICE_VERSION_INFO;
  out_buf[5] = (uint8_t)FW_VERSION_MAJOR;
  out_buf[6] = (uint8_t)FW_VERSION_MINOR;
  out_buf[7] = (uint8_t)FW_VERSION_PATCH;
  out_buf[8] = (uint8_t)HW_VERSION_MAJOR;
  out_buf[9] = (uint8_t)HW_VERSION_MINOR;
  out_buf[10] = (uint8_t)HW_VERSION_PATCH;
  return 0;
}

/* Optional debug helpers (e.g. for CONFIG_LOG_DEFAULT_LEVEL_DBG or tests). */
void payload_hex_dump(const uint8_t *buf, size_t len) {
  if (!buf || len == 0) {
    return;
  }
  printk("PAYLOAD (%u bytes): ", (unsigned)len);
  for (size_t i = 0; i < len; i++) {
    printk("%02X ", buf[i]);
  }
  printk("\n");
}

void payload_decode_log(const uint8_t *buf, size_t len) {
  if (!buf || len < PAYLOAD_LEN_BYTES) {
    LOG_WRN("decode: invalid buffer");
    return;
  }
  uint32_t ts = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
  uint8_t evt = buf[4];

  LOG_INF("Decoded ts=%u evt=0x%02X", ts, evt);

  switch (evt) {
  case EVT_BUTTON: {
    uint8_t btn = buf[5];
    uint32_t ctr =
        ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 8) | (uint32_t)buf[8];
    LOG_INF(" Button: id=%u counter=%u", btn, ctr);
    break;
  }
  case EVT_COUNTER_SYNC: {
    uint8_t btn = buf[5];
    uint32_t ctr =
        ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 8) | (uint32_t)buf[8];
    LOG_INF(" CounterSync: id=%u counter=%u", btn, ctr);
    break;
  }
  default:
    LOG_INF(" Other evt=0x%02X (not decoded)", evt);
    break;
  }
}
