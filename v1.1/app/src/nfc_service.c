/**
 * NFC service: PN5180 ISO15693 scan worker. SMF requests scan; worker runs
 * time-bounded, cancellable scan and posts SMF_EVT_NFC_RESULT. SMF owns rails.
 */
#include "nfc_service.h"
#include "smf_system_mode.h"
#include "sys_config.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "pn5180.h"

LOG_MODULE_REGISTER(nfc_svc, CONFIG_LOG_DEFAULT_LEVEL);

#if !DT_NODE_EXISTS(DT_NODELABEL(pn5180))
#error "pn5180 node not defined in devicetree"
#endif

/* Use sys_config.h for NFC_WORKER_STACK_SIZE */
#define NFC_WORKER_PRIORITY 7
#define NFC_POLL_INTERVAL_MS 200
/** Delay after rails are turned on before using PN5180 (power settle). */
#define NFC_POWER_SETTLE_MS 150

static const struct device *nfc_dev;
static K_SEM_DEFINE(scan_start_sem, 0, 1);
K_SEM_DEFINE(nfc_ready_sem, 0, 1);
static struct k_mutex scan_params_mutex;
static atomic_t scan_intent_atomic = ATOMIC_INIT(0);
static atomic_t scan_button_id_atomic = ATOMIC_INIT(0);
static atomic_t cancel_requested_atomic = ATOMIC_INIT(0);

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

    /* Rails were just turned on by SMF; allow power to settle then re-init
     * PN5180 (it was powered off in idle). */
    k_msleep(NFC_POWER_SETTLE_MS);
    ret = pn5180_init(nfc_dev);
    if (ret != 0) {
      LOG_ERR("NFC scan: pn5180_init failed %d", ret);
      (void)smf_post_nfc_result(0, intent, button_id, NULL);
      goto next_scan;
    }
    ret = pn5180_configure(nfc_dev, PN5180_PROTOCOL_ISO15693);
    if (ret != 0) {
      LOG_ERR("NFC scan: pn5180_configure failed %d", ret);
      (void)smf_post_nfc_result(0, intent, button_id, NULL);
      goto next_scan;
    }

    LOG_INF("NFC scan started (intent=%u btn=%u, block=%d)", intent, button_id,
            NFC_READ_BLOCK);
    deadline_ms = k_uptime_get_32() + NFC_SCAN_TIMEOUT_MS;

    while (k_uptime_get_32() < deadline_ms && atomic_get(&cancel_requested_atomic) == 0) {
      ret = pn5180_get_inventory(nfc_dev, uid, sizeof(uid));
      if (ret != 0) {
        k_msleep(NFC_POLL_INTERVAL_MS);
        continue;
      }
      ret = pn5180_read_block(nfc_dev, uid, NFC_READ_BLOCK, block_data, 4);
      if (ret == 0) {
        LOG_INF("NFC read OK block %d -> uplink", NFC_READ_BLOCK);
        (void)smf_post_nfc_result(1, intent, button_id, block_data);
        (void)pn5180_prepare_poweroff(nfc_dev);
        goto next_scan;
      }
      LOG_DBG("NFC read_block ret=%d", ret);
      k_msleep(NFC_POLL_INTERVAL_MS);
    }

    /* Timeout or cancel: post failure (data_4 not used) */
    LOG_INF("NFC scan timeout/cancel");
    (void)smf_post_nfc_result(0, intent, button_id, NULL);
    (void)pn5180_prepare_poweroff(nfc_dev);

  next_scan:
    (void)0;
  }
}

K_THREAD_DEFINE(nfc_worker_id, NFC_WORKER_STACK_SIZE, nfc_worker_thread, NULL,
               NULL, NULL, NFC_WORKER_PRIORITY, 0, -1);

int nfc_service_init(void) {
  nfc_dev = DEVICE_DT_GET(DT_NODELABEL(pn5180));
  if (!device_is_ready(nfc_dev)) {
    LOG_ERR("PN5180 device not ready");
    return -ENODEV;
  }

  int ret = pn5180_init(nfc_dev);
  if (ret != 0) {
    LOG_ERR("pn5180_init failed: %d", ret);
    return ret;
  }

  ret = pn5180_configure(nfc_dev, PN5180_PROTOCOL_ISO15693);
  if (ret != 0) {
    LOG_ERR("pn5180_configure failed: %d", ret);
    return ret;
  }

  k_mutex_init(&scan_params_mutex);
  LOG_INF("nfc_service initialized (start nfc_worker_id from main)");
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
