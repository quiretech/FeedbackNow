#include "devnonce_store.h"

#include "log_fmt.h"
#include "sys_config.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(devnonce_store, CONFIG_LOG_DEFAULT_LEVEL);

/* EEPROM node */
#if DT_NODE_EXISTS(DT_NODELABEL(eeprom0))
#define EEPROM_NODE DT_NODELABEL(eeprom0)
static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_NODE);
#else
static const struct device *eeprom_dev = NULL;
#endif

/* Storage layout (must not overlap with other EEPROM users) */
#define SLOT_SIZE ((uint32_t)EEPROM_DEVNONCE_SLOT_SIZE)
#define SLOT0_OFF ((off_t)EEPROM_DEVNONCE_SLOT0_OFF)
#define SLOT1_OFF ((off_t)EEPROM_DEVNONCE_SLOT1_OFF)

#define DEVNONCE_MAGIC 0x444E4F4EU /* 'DNON' */
#define DEVNONCE_VERSION 1U

struct __packed devnonce_record {
  uint32_t magic;
  uint8_t version;
  uint8_t reserved0;
  uint16_t last_devnonce;
  uint32_t seq;
  uint32_t crc32;
  uint8_t pad[SLOT_SIZE - 4U - 1U - 1U - 2U - 4U - 4U];
};

static struct {
  struct k_mutex lock;
  bool initialized;
  uint8_t active_slot; /* 0 or 1 */
  uint32_t seq;
  uint16_t last_devnonce;
} ctx;

static uint32_t rec_crc32(const struct devnonce_record *rec) {
  return crc32_ieee((const uint8_t *)rec,
                    offsetof(struct devnonce_record, crc32));
}

static bool rec_is_blank(const struct devnonce_record *rec) {
  const uint8_t *p = (const uint8_t *)rec;
  for (size_t i = 0; i < sizeof(*rec); i++) {
    if (p[i] != 0xFF) {
      return false;
    }
  }
  return true;
}

static bool rec_is_valid(const struct devnonce_record *rec) {
  if (rec->magic != DEVNONCE_MAGIC) {
    return false;
  }
  if (rec->version != DEVNONCE_VERSION) {
    return false;
  }
  return rec_crc32(rec) == rec->crc32;
}

static void build_rec(struct devnonce_record *rec, uint32_t seq,
                      uint16_t last_devnonce) {
  memset(rec, 0, sizeof(*rec));
  rec->magic = DEVNONCE_MAGIC;
  rec->version = DEVNONCE_VERSION;
  rec->reserved0 = 0;
  rec->last_devnonce = last_devnonce;
  rec->seq = seq;
  rec->crc32 = rec_crc32(rec);
}

static int eeprom_read_rec(off_t off, struct devnonce_record *out) {
  return eeprom_read(eeprom_dev, off, out, sizeof(*out));
}

static int eeprom_write_rec(off_t off, const struct devnonce_record *rec) {
  return eeprom_write(eeprom_dev, off, rec, sizeof(*rec));
}

static int load_or_init(void) {
  struct devnonce_record r0 = {0};
  struct devnonce_record r1 = {0};

  int ret = eeprom_read_rec(SLOT0_OFF, &r0);
  if (ret != 0) {
    return ret;
  }
  ret = eeprom_read_rec(SLOT1_OFF, &r1);
  if (ret != 0) {
    return ret;
  }

  const bool blank0 = rec_is_blank(&r0);
  const bool blank1 = rec_is_blank(&r1);
  const bool v0 = rec_is_valid(&r0);
  const bool v1 = rec_is_valid(&r1);

  if ((blank0 && blank1) || (!v0 && !v1)) {
    /* Initialize with a randomized starting point to reduce predictability. */
    uint32_t rnd = 0;
    sys_rand_get(&rnd, sizeof(rnd));
    const uint16_t start = (uint16_t)rnd;

    ctx.active_slot = 0;
    ctx.seq = 0;
    ctx.last_devnonce = start;

    struct devnonce_record init = {0};
    build_rec(&init, ctx.seq, ctx.last_devnonce);
    ret = eeprom_write_rec(SLOT0_OFF, &init);
    if (ret != 0) {
      return ret;
    }

    LOG_SECTION_INF("DEVNONCE STORE INITIALIZED");
    LOG_INF("DevNonce store initialized (slot=%u seq=%u start=%u)",
            ctx.active_slot, ctx.seq, ctx.last_devnonce);
    return 0;
  }

  const struct devnonce_record *best = NULL;
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
  ctx.last_devnonce = best->last_devnonce;

  LOG_INF("DevNonce store loaded (slot=%u seq=%u last=%u)",
          ctx.active_slot, ctx.seq, ctx.last_devnonce);
  return 0;
}

int devnonce_store_init(void) {
  if (ctx.initialized) {
    return 0;
  }
  if (eeprom_dev == NULL) {
    return -ENODEV;
  }
  if (!device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }

  BUILD_ASSERT(SLOT_SIZE >= 16U, "DevNonce slot size too small");
  BUILD_ASSERT((EEPROM_DEVNONCE_SLOT0_OFF % 4U) == 0U,
               "DevNonce slot0 offset should be 4-byte aligned");

  k_mutex_init(&ctx.lock);
  k_mutex_lock(&ctx.lock, K_FOREVER);

  ctx.active_slot = 0;
  ctx.seq = 0;
  ctx.last_devnonce = 0;

  int ret = load_or_init();
  if (ret == 0) {
    ctx.initialized = true;
  }

  k_mutex_unlock(&ctx.lock);
  return ret;
}

int devnonce_store_next(uint16_t *out_nonce) {
  if (out_nonce == NULL) {
    return -EINVAL;
  }
  if (!ctx.initialized) {
    return -EACCES;
  }

  k_mutex_lock(&ctx.lock, K_FOREVER);

  const uint8_t next_slot = (ctx.active_slot == 0) ? 1 : 0;
  const off_t off = (next_slot == 0) ? SLOT0_OFF : SLOT1_OFF;

  /* Reserve the next value by persisting it before returning. */
  const uint16_t next_nonce = (uint16_t)(ctx.last_devnonce + 1U);
  const uint32_t next_seq = ctx.seq + 1U;

  struct devnonce_record rec = {0};
  build_rec(&rec, next_seq, next_nonce);

  k_mutex_unlock(&ctx.lock);

  int ret = eeprom_write_rec(off, &rec);

  k_mutex_lock(&ctx.lock, K_FOREVER);
  if (ret != 0) {
    LOG_ERR("DevNonce persist failed (slot=%u off=0x%x): %d",
            next_slot, (unsigned)off, ret);
    k_mutex_unlock(&ctx.lock);
    return ret;
  }

  ctx.active_slot = next_slot;
  ctx.seq = next_seq;
  ctx.last_devnonce = next_nonce;
  *out_nonce = next_nonce;

  k_mutex_unlock(&ctx.lock);
  return 0;
}

static int erase_devnonce_slots(void) {
  /* Erase both slots by writing 0xFF to them */
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

int devnonce_store_factory_reset(void) {
  if (eeprom_dev == NULL || !device_is_ready(eeprom_dev)) {
    return -ENODEV;
  }

  /* Avoid concurrent access */
  k_mutex_lock(&ctx.lock, K_FOREVER);
  ctx.initialized = false;
  k_mutex_unlock(&ctx.lock);

  LOG_SECTION_WRN("FACTORY RESET: EEPROM DevNonce store");

  int ret = erase_devnonce_slots();
  if (ret != 0) {
    LOG_ERR("Failed to erase DevNonce slots: %d", ret);
    return ret;
  }

  /* Reinitialize with a new random starting value */
  k_mutex_lock(&ctx.lock, K_FOREVER);
  
  uint32_t rnd = 0;
  sys_rand_get(&rnd, sizeof(rnd));
  const uint16_t start = (uint16_t)rnd;

  ctx.active_slot = 0;
  ctx.seq = 0;
  ctx.last_devnonce = start;

  struct devnonce_record init = {0};
  build_rec(&init, ctx.seq, ctx.last_devnonce);
  ret = eeprom_write_rec(SLOT0_OFF, &init);
  if (ret != 0) {
    LOG_ERR("Failed to write initial DevNonce record: %d", ret);
    k_mutex_unlock(&ctx.lock);
    return ret;
  }

  ctx.initialized = true;
  k_mutex_unlock(&ctx.lock);

  LOG_WRN("DevNonce factory reset complete (slot=0 seq=0 start=%u). New random DevNonce initialized.", start);
  return 0;
}


