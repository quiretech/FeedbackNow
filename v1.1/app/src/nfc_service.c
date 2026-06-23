/**
 * NFC service: PN5180 ISO15693 scan worker. SMF requests scan; worker runs
 * time-bounded, cancellable scan and posts SMF_EVT_NFC_RESULT. SMF owns rails.
 *
 * Reliability features:
 *   - Per-scan init recovery loop (cycles 3.6V on repeated failures so a wedged
 *     chip can be cleared without a device reboot).
 *   - Warm-path skip: if the 3.6V keep-alive has held the PN5180 powered since
 *     the last successful scan, we skip pn5180_init+configure (~200 ms) and
 *     dive straight into the inventory loop.
 *   - Boot self-test: validates chip presence and version at start-up; Staff
 *     NFC is disabled (with a warning) if the chip never answers.
 */
#include "nfc_service.h"
#include "log_fmt.h"
#include "rail_manager.h"
#include "smf_system_mode.h"
#include "sys_config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nfc_svc, CONFIG_LOG_DEFAULT_LEVEL);

#if NFC_ENABLED

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/atomic.h>

#include "pn5180.h"

#if !DT_NODE_EXISTS(DT_NODELABEL(pn5180))
#error "pn5180 node not defined in devicetree (FLEXBOX_PLUS / NFC_ENABLED builds)"
#endif

#define NFC_WORKER_PRIORITY 7

static const struct device *nfc_dev;
static K_SEM_DEFINE(scan_start_sem, 0, 1);
K_SEM_DEFINE(nfc_ready_sem, 0, 1);
static struct k_mutex scan_params_mutex;
static atomic_t scan_intent_atomic = ATOMIC_INIT(0);
static atomic_t scan_button_id_atomic = ATOMIC_INIT(0);
static atomic_t cancel_requested_atomic = ATOMIC_INIT(0);

/* Warm skip only after a successful scan ended with prepare_poweroff, and only
 * while still inside the 3.6V keep-alive window (chip has not been powered off).
 * Worker thread only — no lock needed. */
static bool nfc_warm_eligible;
static uint32_t nfc_warm_until_ms;

/**
 * Try pn5180_init + pn5180_configure with a recovery cycle between attempts.
 * Returns 0 on success, negative on persistent failure. Caller must hold 3.6V
 * (via rail_manager_request_3v6) so the recovery pulse stays within ref>0.
 */
static void nfc_cold_bringup(void) {
  LOG_DBG("NFC cold bringup: 3.6V pulse off=%u settle=%u ms",
          NFC_COLD_BOOT_OFF_MS, NFC_POWER_SETTLE_MS);
  rail_manager_pulse_3v6_recovery(NFC_COLD_BOOT_OFF_MS, NFC_POWER_SETTLE_MS);
}

static int nfc_init_with_recovery(void) {
  int ret = -EIO;

  for (int attempt = 1; attempt <= NFC_INIT_MAX_ATTEMPTS; attempt++) {
    if (attempt > 1) {
      uint32_t off_ms = (attempt == 2) ? NFC_RECOVERY_OFF_MS
                                       : NFC_RECOVERY_OFF_MS_ESCALATED;
      uint32_t settle_ms = (attempt == 2) ? NFC_RECOVERY_SETTLE_MS
                                          : NFC_RECOVERY_SETTLE_MS_ESCALATED;
      LOG_WRN("cycling 3.6V for recovery (attempt %d/%d, off=%u ms settle=%u ms)",
              attempt, NFC_INIT_MAX_ATTEMPTS, off_ms, settle_ms);
      rail_manager_pulse_3v6_recovery(off_ms, settle_ms);
    }

    int64_t t0 = k_uptime_get();
    ret = pn5180_init(nfc_dev);
    if (ret != 0) {
      LOG_ERR("pn5180_init failed (attempt %d/%d): %d", attempt,
              NFC_INIT_MAX_ATTEMPTS, ret);
      (void)pn5180_prepare_poweroff(nfc_dev);
      continue;
    }
    ret = pn5180_configure(nfc_dev, PN5180_PROTOCOL_ISO15693);
    if (ret != 0) {
      LOG_ERR("pn5180_configure failed (attempt %d/%d): %d", attempt,
              NFC_INIT_MAX_ATTEMPTS, ret);
      (void)pn5180_prepare_poweroff(nfc_dev);
      continue;
    }
    LOG_DBG("NFC probe ok attempt %d/%d %lld ms", attempt,
            NFC_INIT_MAX_ATTEMPTS, k_uptime_get() - t0);
    return 0;
  }

  LOG_ERR("persistent chip fault after %d attempts; NFC scan aborted",
          NFC_INIT_MAX_ATTEMPTS);
  (void)pn5180_prepare_poweroff(nfc_dev);
  return ret;
}

static void nfc_worker_thread(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  uint8_t uid[8];
  uint8_t block_data[4];
  uint32_t deadline_ms;
  uint8_t intent;
  uint8_t button_id;
  int ret;

  k_sem_give(&nfc_ready_sem);
  while (1) {
    k_sem_take(&scan_start_sem, K_FOREVER);

    k_mutex_lock(&scan_params_mutex, K_FOREVER);
    intent = (uint8_t)atomic_get(&scan_intent_atomic);
    button_id = (uint8_t)atomic_get(&scan_button_id_atomic);
    k_mutex_unlock(&scan_params_mutex);
    atomic_set(&cancel_requested_atomic, 0);

    uint32_t now_ms = k_uptime_get_32();
    bool warm = nfc_warm_eligible && rail_manager_is_3v6_on() &&
                (now_ms < nfc_warm_until_ms);
    if (warm) {
      LOG_DBG("NFC warm skip init (last scan clean shutdown)");
    } else {
      nfc_cold_bringup();
      if (nfc_init_with_recovery() != 0) {
        nfc_warm_eligible = false;
        nfc_warm_until_ms = 0U;
        (void)smf_post_nfc_result(0, intent, button_id, NULL);
        goto next_scan;
      }
    }

    LOG_DBG("NFC scan i=%u btn=%u blk=%d", intent, button_id,
            NFC_READ_BLOCK);
    LOG_STATE("NFC scan start intent=%u btn=%u", intent, button_id);
    uint32_t scan_t0 = k_uptime_get_32();
    deadline_ms = scan_t0 + NFC_SCAN_PHASE1_MS;
    const uint32_t deadline_cap = scan_t0 + NFC_SCAN_TOTAL_MS;
    bool saw_inventory = false;

    while (k_uptime_get_32() < deadline_ms &&
           atomic_get(&cancel_requested_atomic) == 0) {
      ret = pn5180_get_inventory(nfc_dev, uid, sizeof(uid));
      if (ret != 0) {
        k_msleep(NFC_POLL_INTERVAL_MS);
        continue;
      }
      if (!saw_inventory) {
        saw_inventory = true;
        if (deadline_ms < deadline_cap) {
          deadline_ms = deadline_cap;
        }
      }
      ret = pn5180_read_block(nfc_dev, uid, NFC_READ_BLOCK, block_data, 4);
      if (ret == 0) {
        LOG_STATE("NFC read ok blk=%d", NFC_READ_BLOCK);
        (void)smf_post_nfc_result(1, intent, button_id, block_data);
        (void)pn5180_prepare_poweroff(nfc_dev);
        nfc_warm_eligible = true;
        nfc_warm_until_ms = k_uptime_get_32() + RAIL_MANAGER_3V6_KEEPALIVE_MS;
        goto next_scan;
      }
      LOG_DBG("NFC read_block ret=%d", ret);
      k_msleep(NFC_POLL_INTERVAL_MS);
    }

    /* Timeout or cancel: post failure. Be defensive — a timeout could mean
     * "no tag presented" (chip healthy) OR "chip wedged mid-scan" (chip bad).
     * We clear nfc_chip_alive so the next scan re-inits via the recovery
     * loop. Costs ~200 ms on the next timeout path; earns auto-healing. */
    LOG_STATE("NFC scan end (timeout/cancel)");
    (void)smf_post_nfc_result(0, intent, button_id, NULL);
    (void)pn5180_prepare_poweroff(nfc_dev);
    nfc_warm_eligible = false;
    nfc_warm_until_ms = 0U;

  next_scan:
    (void)0;
  }
}

K_THREAD_DEFINE(nfc_worker_id, NFC_WORKER_STACK_SIZE, nfc_worker_thread, NULL,
                NULL, NULL, NFC_WORKER_PRIORITY, 0, -1);

/**
 * Boot-time self-test: powers 3.6V, runs init+version-read, powers off.
 * Confirms the chip is present and responsive. Failure logs a clear error
 * but does NOT disable NFC — the per-scan recovery loop can still recover.
 */
static int nfc_service_self_test(void) {
  struct pn5180_version_info info;
  int ret;

  k_msleep(NFC_POWER_SETTLE_MS);

  ret = pn5180_init(nfc_dev);
  if (ret != 0) {
    LOG_ERR("SELF-TEST FAILED: pn5180_init=%d", ret);
    return ret;
  }

  ret = pn5180_get_version(nfc_dev, &info);
  if (ret != 0) {
    LOG_ERR("SELF-TEST FAILED: pn5180_get_version=%d", ret);
    (void)pn5180_prepare_poweroff(nfc_dev);
    return ret;
  }

  LOG_DBG("NFC test ok p=%u.%u fw=%u.%u eeprom=%u.%u",
          (info.product_version >> 8) & 0xFFU, info.product_version & 0xFFU,
          (info.firmware_version >> 8) & 0xFFU, info.firmware_version & 0xFFU,
          (info.eeprom_version >> 8) & 0xFFU, info.eeprom_version & 0xFFU);

  (void)pn5180_prepare_poweroff(nfc_dev);
  return 0;
}

int nfc_service_init(void) {
  nfc_dev = DEVICE_DT_GET(DT_NODELABEL(pn5180));
  if (!device_is_ready(nfc_dev)) {
    LOG_ERR("PN5180 device not ready");
    return -ENODEV;
  }

  k_mutex_init(&scan_params_mutex);
  nfc_warm_eligible = false;
  nfc_warm_until_ms = 0U;

  /* Self-test runs while boot rails are still manually held on by main() —
   * no rail_manager request needed here. */
  int ret = nfc_service_self_test();
  if (ret != 0) {
    LOG_WRN("self-test failed; per-scan recovery will try to recover");
  }

  LOG_STATE("nfc_service ready");
  return 0;
}

int nfc_service_wait_until_ready(k_timeout_t timeout) {
  return k_sem_take(&nfc_ready_sem, timeout);
}

void nfc_scan_start(uint8_t intent, uint8_t button_id) {
  k_mutex_lock(&scan_params_mutex, K_FOREVER);
  atomic_set(&scan_intent_atomic, intent);
  atomic_set(&scan_button_id_atomic, button_id);
  k_mutex_unlock(&scan_params_mutex);
  k_sem_give(&scan_start_sem);
}

void nfc_scan_cancel(void) {
  atomic_set(&cancel_requested_atomic, 1);
}

#else /* !NFC_ENABLED — FLEXBOX: no PN5180 on board */

static K_SEM_DEFINE(nfc_ready_sem, 1, 1);

int nfc_service_init(void) {
  LOG_DBG("NFC disabled (DEVICE_HW_VARIANT FLEXBOX)");
  return 0;
}

int nfc_service_wait_until_ready(k_timeout_t timeout) {
  ARG_UNUSED(timeout);
  return 0;
}

void nfc_scan_start(uint8_t intent, uint8_t button_id) {
  ARG_UNUSED(intent);
  ARG_UNUSED(button_id);
}

void nfc_scan_cancel(void) {
}

static void nfc_worker_disabled(void *a, void *b, void *c) {
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  for (;;) {
    k_sleep(K_FOREVER);
  }
}

K_THREAD_DEFINE(nfc_worker_id, 256, nfc_worker_disabled, NULL, NULL, NULL, 7, 0,
                -1);

#endif /* NFC_ENABLED */
