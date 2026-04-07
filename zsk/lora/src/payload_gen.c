#include "payload_gen.h"

#include "button_counter_store.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(payload_gen, CONFIG_LOG_DEFAULT_LEVEL);

#define BUTTON_ID_MAX 6
#define BUTTON_COUNTER_ROLLOVER (1U << 24) /* 24-bit counter */
#define BATTERY_MIN 0
#define BATTERY_MAX 100

static struct {
  uint8_t nfc_uid[6];
  uint8_t next_button_id;
  enum payload_event_type next_evt;
  uint32_t base_epoch; /* pseudo-UTC seed per boot */
  struct k_mutex lock;
} ctx;

static uint32_t get_epoch_seconds(void) {
  /* Pseudo-UTC: seeded once at boot, then incremented with uptime seconds */
  return ctx.base_epoch + (uint32_t)(k_uptime_get() / 1000U);
}

static void write_be32(uint8_t *buf, uint32_t v) {
  buf[0] = (uint8_t)(v >> 24);
  buf[1] = (uint8_t)(v >> 16);
  buf[2] = (uint8_t)(v >> 8);
  buf[3] = (uint8_t)(v);
}

static uint8_t rand_in_range(uint8_t lo, uint8_t hi) {
  uint32_t r = 0;
  sys_rand_get(&r, sizeof(r));
  return (uint8_t)(lo + (r % (uint32_t)(hi - lo + 1U)));
}

void payload_gen_init(void) {
  k_mutex_init(&ctx.lock);
  k_mutex_lock(&ctx.lock, K_FOREVER);

  /* Seed an initial NFC UID (will be randomized per NFC uplink) */
  sys_rand_get(ctx.nfc_uid, sizeof(ctx.nfc_uid));

  /* Seed a plausible UTC base (~2023-11-14 epoch 1700000000) + up to ~1 year */
  uint32_t r = 0;
  sys_rand_get(&r, sizeof(r));
  ctx.base_epoch = 1700000000U + (r % 31536000U); /* +/- ~1 year window */

  ctx.next_button_id = rand_in_range(0, BUTTON_ID_MAX);
  ctx.next_evt = EVT_BUTTON;

  k_mutex_unlock(&ctx.lock);

  LOG_INF("payload_gen initialized");
}

static void build_button(uint8_t *b) {
  uint8_t btn_id = ctx.next_button_id;

  write_be32(b, get_epoch_seconds());
  b[4] = EVT_BUTTON;
  b[5] = btn_id;
  uint32_t c = 0;
  if (button_counter_store_inc(btn_id, &c) != 0) {
    /* Should not happen in production (EEPROM is mandatory); fall back to 0 */
    c = 0;
  }
  b[6] = (uint8_t)(c >> 16);
  b[7] = (uint8_t)(c >> 8);
  b[8] = (uint8_t)(c);
  b[9] = 0x00;
  b[10] = 0x00;

  /* Cycle button ID 0..6 */
  ctx.next_button_id = (ctx.next_button_id + 1U) % (BUTTON_ID_MAX + 1U);
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
    /* Should not happen in production (EEPROM is mandatory); fall back to 0 */
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

static void build_nfc(uint8_t *b) {
  write_be32(b, get_epoch_seconds());
  b[4] = EVT_NFC;
  /* Randomize NFC UID on every uplink */
  sys_rand_get(ctx.nfc_uid, sizeof(ctx.nfc_uid));
  for (int i = 0; i < 6; i++) {
    b[5 + i] = ctx.nfc_uid[i];
  }
}

static void build_battery(uint8_t *b) {
  write_be32(b, get_epoch_seconds());
  b[4] = EVT_BATTERY;
  b[5] = rand_in_range(BATTERY_MIN, BATTERY_MAX);
  for (int i = 6; i < 11; i++) {
    b[i] = 0x00;
  }
}

static void build_future(uint8_t *b) {
  write_be32(b, get_epoch_seconds());
  b[4] = EVT_FUTURE;
  for (int i = 5; i < 11; i++) {
    b[i] = 0x00;
  }
}

int payload_gen_next(uint8_t *out_buf, uint8_t *out_fport) {
  if (!out_buf || !out_fport) {
    return -EINVAL;
  }

  k_mutex_lock(&ctx.lock, K_FOREVER);

  enum payload_event_type evt = ctx.next_evt;

  switch (evt) {
  case EVT_BUTTON:
    build_button(out_buf);
    *out_fport = FPORT_BUTTON;
    ctx.next_evt = EVT_NFC;
    break;
  case EVT_NFC:
    build_nfc(out_buf);
    *out_fport = FPORT_NFC;
    ctx.next_evt = EVT_BATTERY;
    break;
  case EVT_BATTERY:
    build_battery(out_buf);
    *out_fport = FPORT_BATTERY;
    ctx.next_evt = EVT_BUTTON;
    break;
  default:
    build_future(out_buf);
    *out_fport = FPORT_FUTURE;
    ctx.next_evt = EVT_BUTTON;
    break;
  }

  k_mutex_unlock(&ctx.lock);
  return 0;
}

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
  case EVT_NFC:
    LOG_INF(" NFC UID: %02X %02X %02X %02X %02X %02X", buf[5], buf[6], buf[7],
            buf[8], buf[9], buf[10]);
    break;
  case EVT_BATTERY:
    LOG_INF(" Battery: %u%%", buf[5]);
    break;
  default:
    LOG_INF(" Future/reserved payload");
    break;
  }
}
