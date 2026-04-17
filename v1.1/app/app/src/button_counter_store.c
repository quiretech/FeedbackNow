/*
 * Persistent per-button counters stored in external AT24-compatible EEPROM.
 *
 * Design:
 * - Two fixed slots (A/B) with seq + CRC32 for power-loss safety.
 * - Counters cached in RAM and flushed to EEPROM after a short delay.
 * - EEPROM is mandatory for production; callers can hard-fail if init fails.
 */

#include "button_counter_store.h"
#include "rail_manager.h"

#include "log_fmt.h"
#include "sys_config.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(button_counter_store, CONFIG_LOG_DEFAULT_LEVEL);

/* EEPROM node */
#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
#define EEPROM_NODE DT_NODELABEL(eeprom0)
static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_NODE);
#else
static const struct device *eeprom_dev = NULL;
#endif

/* Storage layout: 2 slots of fixed size at EEPROM offset 0 */
#define SLOT_SIZE 256U
#define SLOT0_OFF 0U
#define SLOT1_OFF (SLOT0_OFF + SLOT_SIZE)

/* Flush delay: batch presses to reduce EEPROM wear */
#define FLUSH_DELAY K_SECONDS(5)

/* Record format */
#define COUNTER_MAGIC 0x43544E52U /* 'CTNR' */
#define COUNTER_VERSION 1U

struct __packed counter_record {
  uint32_t magic;
  uint8_t version;
  uint8_t num_buttons;
  uint16_t reserved;
  uint32_t seq;
  uint32_t counters[NUM_BUTTONS];
  uint32_t crc32;
};

static struct {
  struct k_mutex lock;
  struct k_work_delayable flush_work;
  bool initialized;
  bool dirty;
  uint8_t active_slot; /* 0 or 1 */
  uint32_t seq;
  uint32_t counters[NUM_BUTTONS];
} ctx;

static uint32_t record_crc32(const struct counter_record *rec) {
  /* CRC over everything except the crc32 field */
  return crc32_ieee((const uint8_t *)rec,
                    offsetof(struct counter_record, crc32));
}

static int eeprom_read_rec(off_t off, struct counter_record *out) {
  int ret = eeprom_read(eeprom_dev, off, out, sizeof(*out));
  if (ret != 0) {
    return ret;
  }
  return 0;
}

static int eeprom_write_rec(off_t off, const struct counter_record *rec) {
  return eeprom_write(eeprom_dev, off, rec, sizeof(*rec));
}

static bool record_is_blank(const struct counter_record *rec) {
  /* If EEPROM is erased, all bytes read as 0xFF */
  const uint8_t *p = (const uint8_t *)rec;
  for (size_t i = 0; i < sizeof(*rec); i++) {
    if (p[i] != 0xFF) {
      return false;
    }
  }
  return true;
}

static bool record_is_valid(const struct counter_record *rec) {
  if (rec->magic != COUNTER_MAGIC) {
    return false;
  }
  if (rec->version != COUNTER_VERSION) {
    return false;
  }
  if (rec->num_buttons != NUM_BUTTONS) {
    return false;
  }
  return record_crc32(rec) == rec->crc32;
}

static void build_record(struct counter_record *rec, uint32_t seq,
                         const uint32_t counters[NUM_BUTTONS]) {
  memset(rec, 0, sizeof(*rec));
  rec->magic = COUNTER_MAGIC;
  rec->version = COUNTER_VERSION;
  rec->num_buttons = NUM_BUTTONS;
  rec->reserved = 0;
  rec->seq = seq;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    rec->counters[i] = counters[i];
  }
  rec->crc32 = record_crc32(rec);
}

static int load_from_eeprom(void) {
  struct counter_record r0 = {0};
  struct counter_record r1 = {0};

  int ret = eeprom_read_rec(SLOT0_OFF, &r0);
  if (ret != 0) {
    return ret;
  }
  ret = eeprom_read_rec(SLOT1_OFF, &r1);
  if (ret != 0) {
    return ret;
  }

  const bool blank0 = record_is_blank(&r0);
  const bool blank1 = record_is_blank(&r1);
  const bool v0 = record_is_valid(&r0);
  const bool v1 = record_is_valid(&r1);

  if ((blank0 && blank1) || (!v0 && !v1)) {
    LOG_WRN("Counter store not initialized; starting fresh (blank=%d/%d "
            "valid=%d/%d)",
            blank0, blank1, v0, v1);
    ctx.active_slot = 0;
    ctx.seq = 0;
    for (int i = 0; i < NUM_BUTTONS; i++) {
      ctx.counters[i] = 0;
    }

    /* Write initial record to slot0 */
    struct counter_record init = {0};
    build_record(&init, ctx.seq, ctx.counters);
    ret = eeprom_write_rec(SLOT0_OFF, &init);
    if (ret != 0) {
      return ret;
    }
    LOG_INF("Counter store initialized in EEPROM (slot=%u seq=%u)",
            ctx.active_slot, ctx.seq);
    LOG_INF("Button counters (boot): b0=%u b1=%u b2=%u b3=%u b4=%u b5=%u",
            ctx.counters[0], ctx.counters[1], ctx.counters[2], ctx.counters[3],
            ctx.counters[4], ctx.counters[5]);
    return 0;
  }

  const struct counter_record *best = NULL;
  uint8_t best_slot = 0;
  if (v0 && v1) {
    if (r1.seq >= r0.seq) {
      best = &r1;
      best_slot = 1;
    } else {
      best = &r0;
      best_slot = 0;
    }
  } else if (v0) {
    best = &r0;
    best_slot = 0;
  } else {
    best = &r1;
    best_slot = 1;
  }

  ctx.active_slot = best_slot;
  ctx.seq = best->seq;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    ctx.counters[i] = best->counters[i];
  }

  LOG_INF("Counter store loaded: slot=%u seq=%u", ctx.active_slot, ctx.seq);
  LOG_INF("Button counters (boot): b0=%u b1=%u b2=%u b3=%u b4=%u b5=%u",
          ctx.counters[0], ctx.counters[1], ctx.counters[2], ctx.counters[3],
          ctx.counters[4], ctx.counters[5]);
  return 0;
}

static void flush_work_handler(struct k_work *work) {
  ARG_UNUSED(work);

  k_mutex_lock(&ctx.lock, K_FOREVER);
  if (!ctx.dirty) {
    k_mutex_unlock(&ctx.lock);
    return;
  }

  const uint8_t next_slot = (ctx.active_slot == 0) ? 1 : 0;
  const off_t off = (next_slot == 0) ? (off_t)SLOT0_OFF : (off_t)SLOT1_OFF;
  const uint32_t next_seq = ctx.seq + 1U;

  struct counter_record rec = {0};
  build_record(&rec, next_seq, ctx.counters);

  k_mutex_unlock(&ctx.lock);

  rail_manager_request_3v3a();
  int ret = eeprom_write_rec(off, &rec);
  rail_manager_release_3v3a();

  k_mutex_lock(&ctx.lock, K_FOREVER);
  if (ret != 0) {
    LOG_ERR("EEPROM counter flush failed (slot=%u off=0x%x): %d", next_slot,
            (unsigned)off, ret);
    /* Keep dirty=true so we retry on next schedule */
    k_mutex_unlock(&ctx.lock);
    return;
  }

  ctx.active_slot = next_slot;
  ctx.seq = next_seq;
  ctx.dirty = false;
  LOG_INF("EEPROM counter flush OK (slot=%u seq=%u)", ctx.active_slot, ctx.seq);
  k_mutex_unlock(&ctx.lock);
}

static int erase_counter_slots(void) {
  /* Erase just the two slots we use, not the entire EEPROM. */
  uint8_t ff[SLOT_SIZE];
  memset(ff, 0xFF, sizeof(ff));

  int ret = eeprom_write(eeprom_dev, SLOT0_OFF, ff, sizeof(ff));
  if (ret != 0) {
    return ret;
  }
  ret = eeprom_write(eeprom_dev, SLOT1_OFF, ff, sizeof(ff));
  if (ret != 0) {
    return ret;
  }
  return 0;
}

int button_counter_store_init(void) {
  if (ctx.initialized) {
    return 0;
  }

  if (eeprom_dev == NULL) {
    return -ENODEV;
  }
  if (!device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }

  k_mutex_init(&ctx.lock);
  k_work_init_delayable(&ctx.flush_work, flush_work_handler);

  k_mutex_lock(&ctx.lock, K_FOREVER);
  ctx.dirty = false;
  ctx.active_slot = 0;
  ctx.seq = 0;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    ctx.counters[i] = 0;
  }
  k_mutex_unlock(&ctx.lock);

  int ret = load_from_eeprom();
  if (ret != 0) {
    LOG_ERR("Failed to load counters from EEPROM: %d", ret);
    return ret;
  }

  ctx.initialized = true;
  return 0;
}

int button_counter_store_factory_reset(void) {
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }

  /* Avoid flushing while we reset */
  (void)k_work_cancel_delayable(&ctx.flush_work);

  k_mutex_lock(&ctx.lock, K_FOREVER);
  ctx.dirty = false;
  k_mutex_unlock(&ctx.lock);

  LOG_SECTION_WRN("FACTORY RESET: EEPROM button counters");

  int ret = erase_counter_slots();
  if (ret != 0) {
    LOG_ERR("Failed to erase counter slots: %d", ret);
    return ret;
  }

  k_mutex_lock(&ctx.lock, K_FOREVER);
  ctx.active_slot = 0;
  ctx.seq = 0;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    ctx.counters[i] = 0;
  }
  k_mutex_unlock(&ctx.lock);

  struct counter_record init = {0};
  build_record(&init, 0, ctx.counters);
  ret = eeprom_write_rec(SLOT0_OFF, &init);
  if (ret != 0) {
    LOG_ERR("Failed to write initial counter record: %d", ret);
    return ret;
  }

  LOG_WRN("Factory reset complete (slot=0 seq=0). Counters set to zero.");
  return 0;
}

int button_counter_store_inc(uint8_t button_id, uint32_t *out_counter) {
  if (!ctx.initialized) {
    return -EACCES;
  }
  if (button_id >= NUM_BUTTONS) {
    return -EINVAL;
  }

  k_mutex_lock(&ctx.lock, K_FOREVER);
  ctx.counters[button_id] += 1U;
  const uint32_t v = ctx.counters[button_id];
  ctx.dirty = true;
  k_mutex_unlock(&ctx.lock);

  (void)k_work_schedule(&ctx.flush_work, FLUSH_DELAY);

  if (out_counter) {
    *out_counter = v;
  }
  return 0;
}

int button_counter_store_get(uint8_t button_id, uint32_t *out_counter) {
  if (!ctx.initialized) {
    return -EACCES;
  }
  if (out_counter == NULL) {
    return -EINVAL;
  }
  if (button_id >= NUM_BUTTONS) {
    return -EINVAL;
  }

  k_mutex_lock(&ctx.lock, K_FOREVER);
  *out_counter = ctx.counters[button_id];
  k_mutex_unlock(&ctx.lock);
  return 0;
}
