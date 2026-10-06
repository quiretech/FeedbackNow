/**
 * @file ble_manager.c
 * @brief Bluetooth Low Energy lifecycle and advertising manager implementation.
 */

#include "ble_manager.h"
#include "ble_fbn_service.h"
#include "config_store.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_mgr, CONFIG_LOG_DEFAULT_LEVEL);

static struct bt_conn *current_conn;
static bool is_advertising;
static struct k_mutex ble_lock;

/* Advertising timeout work item */
static void adv_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(adv_timeout_work, adv_timeout_handler);

/* Advertising payload: Flags + Complete Device Name in primary ad packet */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Scan response: 128-bit FeedbackNow Service UUID */
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_FBN_VAL),
};

static void adv_timeout_handler(struct k_work *work) {
  ARG_UNUSED(work);
  LOG_INF("BLE advertising timed out (%u s); stopping radio to conserve battery",
          BLE_ADV_TIMEOUT_SECONDS);
  (void)ble_manager_stop_advertising();
}

static void connected_cb(struct bt_conn *conn, uint8_t err) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  if (err != 0) {
    LOG_ERR("BLE connection failed (err %u) with %s", err, addr);
    return;
  }

  k_mutex_lock(&ble_lock, K_FOREVER);
  current_conn = bt_conn_ref(conn);
  is_advertising = false;
  /* Stop advertising timeout work so connection stays active */
  k_work_cancel_delayable(&adv_timeout_work);
  k_mutex_unlock(&ble_lock);

  LOG_INF("BLE connected to %s", addr);
}

static void restart_adv_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  (void)ble_manager_start_advertising();
}
static K_WORK_DELAYABLE_DEFINE(restart_adv_work, restart_adv_work_handler);

static void disconnected_cb(struct bt_conn *conn, uint8_t reason) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  k_mutex_lock(&ble_lock, K_FOREVER);
  if (current_conn != NULL) {
    bt_conn_unref(current_conn);
    current_conn = NULL;
  }
  is_advertising = false;
  k_mutex_unlock(&ble_lock);

  LOG_INF("BLE disconnected from %s (reason 0x%02X)", addr, reason);

  /* After disconnection, defer advertising restart slightly to allow controller to settle */
  k_work_schedule(&restart_adv_work, K_MSEC(150));
}

static void le_param_updated_cb(struct bt_conn *conn, uint16_t interval,
                                uint16_t latency, uint16_t timeout) {
  LOG_INF("BLE conn params updated: interval=%u latency=%u timeout=%u",
          interval, latency, timeout);
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level,
                                enum bt_security_err err) {
  LOG_INF("BLE security changed: level=%u err=%u", (unsigned)level, (unsigned)err);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
    .le_param_updated = le_param_updated_cb,
    .security_changed = security_changed_cb,
};

int ble_manager_init(void) {
  k_mutex_init(&ble_lock);

  LOG_INF("Initializing Bluetooth stack...");
  int ret = bt_enable(NULL);
  if (ret != 0) {
    LOG_ERR("Bluetooth initialization failed: %d", ret);
    return ret;
  }

  LOG_INF("Bluetooth initialized successfully");

  /* Initialize FeedbackNow Custom GATT Service */
  (void)ble_fbn_service_init();

  /* Automatically start commissioning advertising on boot */
  ret = ble_manager_start_advertising();
  return ret;
}

int ble_manager_start_advertising(void) {
  k_mutex_lock(&ble_lock, K_FOREVER);

  if (current_conn != NULL) {
    LOG_DBG("BLE already connected; skipping advertising");
    k_mutex_unlock(&ble_lock);
    return 0;
  }

  if (is_advertising) {
    /* Restart timeout window */
    k_work_reschedule(&adv_timeout_work, K_SECONDS(BLE_ADV_TIMEOUT_SECONDS));
    k_mutex_unlock(&ble_lock);
    return 0;
  }

  int ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (ret == -EALREADY) {
    ret = 0;
  }
  if (ret != 0) {
    LOG_ERR("Failed to start BLE advertising: %d", ret);
    k_mutex_unlock(&ble_lock);
    return ret;
  }

  is_advertising = true;
  k_work_reschedule(&adv_timeout_work, K_SECONDS(BLE_ADV_TIMEOUT_SECONDS));
  LOG_INF("BLE advertising started for %u seconds (Name: %s)",
          BLE_ADV_TIMEOUT_SECONDS, CONFIG_BT_DEVICE_NAME);

  k_mutex_unlock(&ble_lock);
  return 0;
}

int ble_manager_stop_advertising(void) {
  k_mutex_lock(&ble_lock, K_FOREVER);

  if (!is_advertising) {
    k_mutex_unlock(&ble_lock);
    return 0;
  }

  int ret = bt_le_adv_stop();
  if (ret != 0) {
    LOG_WRN("bt_le_adv_stop failed: %d", ret);
  }

  is_advertising = false;
  k_work_cancel_delayable(&adv_timeout_work);
  LOG_INF("BLE advertising stopped");

  k_mutex_unlock(&ble_lock);
  return ret;
}

bool ble_manager_is_advertising(void) {
  return is_advertising;
}

bool ble_manager_is_connected(void) {
  return (current_conn != NULL);
}
