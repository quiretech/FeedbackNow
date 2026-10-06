/**
 * @file ble_fbn_service.c
 * @brief FeedbackNow Custom Bluetooth LE GATT Service Implementation.
 */

#include "ble_fbn_service.h"
#include "ble_manager.h"
#include "config_store.h"
#include "battery_adc.h"
#include "boot_info.h"
#include "button_counter_store.h"
#include "display_manager.h"
#include "last_cleaned_store.h"
#include "lora_app.h"
#include "lora_link_stats.h"
#include "rtc.h"
#include "sys_config.h"
#include "tz_offset_store.h"

#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(ble_fbn_svc, CONFIG_LOG_DEFAULT_LEVEL);

static bool diag_notify_enabled;

/* Delayed reboot work handler */
static void delayed_reboot_handler(struct k_work *work) {
  ARG_UNUSED(work);
  LOG_INF("BLE requested reboot executing now");
  sys_reboot(SYS_REBOOT_COLD);
}
static K_WORK_DELAYABLE_DEFINE(delayed_reboot_work, delayed_reboot_handler);

/* --- Configuration Characteristic Handlers --- */

static ssize_t read_config(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset) {
  struct fbn_ble_config_read_resp resp;
  memset(&resp, 0, sizeof(resp));

  resp.magic = 0xFB;
  resp.version = 0x01;
  resp.is_provisioned = config_store_is_provisioned() ? 1 : 0;
  resp.lora_region = config_store_get_region();

  config_store_get_dev_eui(resp.dev_eui);
  config_store_get_join_eui(resp.join_eui);
  config_store_get_app_key(resp.app_key);

  const char *uid = config_store_get_unit_id();
  if (uid != NULL) {
    strncpy(resp.unit_id, uid, sizeof(resp.unit_id) - 1);
  }

  const char *client = config_store_get_client_name();
  if (client != NULL) {
    strncpy(resp.client_name, client, sizeof(resp.client_name) - 1);
  }

  resp.provision_epoch = config_store_get_provision_epoch();

  LOG_DBG("BLE read config: unit=%s provisioned=%d", resp.unit_id, resp.is_provisioned);
  return bt_gatt_attr_read(conn, attr, buf, len, offset, &resp, sizeof(resp));
}

static ssize_t write_config(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            const void *buf, uint16_t len, uint16_t offset,
                            uint8_t flags) {
  ARG_UNUSED(conn);
  ARG_UNUSED(attr);
  ARG_UNUSED(flags);

  if (offset != 0 || len < 1) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }

  const uint8_t *data = (const uint8_t *)buf;
  uint8_t cmd = data[0];

  switch (cmd) {
  case FBN_CFG_CMD_SET_LORA_KEYS: {
    /* Expected payload: [cmd(1), dev_eui(8), join_eui(8), app_key(16), region(1)] = 34 bytes */
    if (len < 34) {
      LOG_ERR("BLE set_keys invalid length: %u", len);
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    const uint8_t *dev_eui  = &data[1];
    const uint8_t *join_eui = &data[9];
    const uint8_t *app_key  = &data[17];
    uint8_t region          = data[33];

    int ret = config_store_set_lora_keys(dev_eui, join_eui, app_key, region);
    if (ret != 0) {
      return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    LOG_INF("BLE provisioned new LoRa keys; triggering rejoin");
    lora_prepare_deliberate_rejoin();
    lora_request_join();
    break;
  }

  case FBN_CFG_CMD_SET_IDENTITY: {
    /* Expected payload: [cmd(1), unit_id(16), client_name(24)] = up to 41 bytes */
    char unit_id[CONFIG_STORE_UNIT_ID_LEN] = {0};
    char client_name[CONFIG_STORE_CLIENT_NAME_LEN] = {0};

    if (len > 1) {
      size_t u_len = MIN((size_t)(len - 1), sizeof(unit_id) - 1);
      memcpy(unit_id, &data[1], u_len);
    }
    if (len > 1 + CONFIG_STORE_UNIT_ID_LEN) {
      size_t c_len = MIN((size_t)(len - 1 - CONFIG_STORE_UNIT_ID_LEN), sizeof(client_name) - 1);
      memcpy(client_name, &data[1 + CONFIG_STORE_UNIT_ID_LEN], c_len);
    }

    int ret = config_store_set_identity(unit_id, client_name);
    if (ret != 0) {
      return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    LOG_INF("BLE updated identity: unit=%s client=%s", unit_id, client_name);
    break;
  }

  case FBN_CFG_CMD_SYNC_RTC: {
    /* Expected payload: [cmd(1), epoch_sec(4, BE), tz_offset_min(2, BE)] = 7 bytes */
    if (len < 7) {
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint32_t epoch = sys_get_be32(&data[1]);
    int16_t tz_offset = (int16_t)sys_get_be16(&data[5]);

    int rtc_ret = rtc_set_epoch_seconds(epoch);
    (void)tz_offset_store_set(tz_offset);

    LOG_INF("BLE synchronized RTC: epoch=%u tz_min=%d (res=%d)", epoch, tz_offset, rtc_ret);
#if EPD_ENABLED
    display_show_logo();
#endif
    break;
  }

  case FBN_CFG_CMD_RESET_CONFIG: {
    int ret = config_store_factory_reset();
    if (ret != 0) {
      return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    LOG_INF("BLE reset config to compile-time defaults");
    lora_prepare_deliberate_rejoin();
    lora_request_join();
    break;
  }

  default:
    LOG_WRN("Unknown config command: 0x%02X", cmd);
    return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
  }

  return len;
}

/* --- Diagnostics Characteristic Handlers --- */

static void gather_diag_payload(struct fbn_ble_diag_read_resp *resp) {
  memset(resp, 0, sizeof(*resp));

  int32_t mv = 0;
  if (battery_adc_read_mv(&mv) == 0 && mv > 0) {
    resp->battery_mv = (uint16_t)mv;
  } else if (battery_adc_last_mv_get(&mv) == 0 && mv > 0) {
    resp->battery_mv = (uint16_t)mv;
  }

  uint32_t epoch = 0;
  (void)rtc_get_epoch_seconds(&epoch);
  resp->rtc_epoch = epoch;

  resp->uptime_seconds = (uint32_t)(k_uptime_get() / 1000);
  resp->lora_joined = lora_is_joined() ? 1 : 0;

  lora_link_stats_snapshot_t stats;
  if (lora_link_stats_get(&stats)) {
    resp->last_demod_margin = stats.last_demod_margin;
    resp->last_nb_gateways = stats.last_nb_gateways;
    resp->link_check_samples = stats.samples;
  } else {
    resp->last_demod_margin = LORA_LINK_STATS_MARGIN_NONE;
  }

  for (uint8_t i = 0; i < 6; i++) {
    uint32_t count = 0;
    (void)button_counter_store_get(i, &count);
    resp->button_counts[i] = count;
  }

  uint32_t cleaned_epoch = 0;
  (void)last_cleaned_store_get(&cleaned_epoch);
  resp->last_cleaned_epoch = cleaned_epoch;
  resp->boot_cause = (uint8_t)boot_info_get_cause();
  resp->hw_variant = (uint8_t)DEVICE_HW_VARIANT;
  resp->fw_version_major = (uint8_t)FW_VERSION_MAJOR;
  resp->fw_version_minor = (uint8_t)FW_VERSION_MINOR;
  resp->fw_version_patch = (uint8_t)FW_VERSION_PATCH;
}

static ssize_t read_diag(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          void *buf, uint16_t len, uint16_t offset) {
  struct fbn_ble_diag_read_resp resp;
  gather_diag_payload(&resp);

  LOG_DBG("BLE read diag: bat=%umV epoch=%u joined=%u",
          (unsigned)resp.battery_mv, (unsigned)resp.rtc_epoch, (unsigned)resp.lora_joined);

  return bt_gatt_attr_read(conn, attr, buf, len, offset, &resp, sizeof(resp));
}

static void diag_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value) {
  ARG_UNUSED(attr);
  diag_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
  LOG_INF("BLE diagnostics notification %s", diag_notify_enabled ? "enabled" : "disabled");
}

/* --- Control Characteristic Handlers --- */

static ssize_t write_ctrl(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          const void *buf, uint16_t len, uint16_t offset,
                          uint8_t flags) {
  ARG_UNUSED(conn);
  ARG_UNUSED(attr);
  ARG_UNUSED(flags);

  if (offset != 0 || len < 1) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }

  uint8_t cmd = ((const uint8_t *)buf)[0];
  LOG_INF("BLE control command received: 0x%02X", cmd);

  switch (cmd) {
  case FBN_CTRL_CMD_LORA_JOIN:
    lora_prepare_deliberate_rejoin();
    lora_request_join();
    break;

  case FBN_CTRL_CMD_LORA_TEST_UPLINK: {
    lora_uplink_msg_t test_msg = {
        .port = 1,
        .len = 2,
        .confirmed = false,
        .data = {0xFE, 0x01},
    };
    (void)lora_put_event(&test_msg, K_NO_WAIT);
    break;
  }

  case FBN_CTRL_CMD_CLEAR_COUNTERS:
    (void)button_counter_store_factory_reset();
    LOG_INF("BLE cleared button counters");
    break;

  case FBN_CTRL_CMD_REFRESH_DISPLAY:
#if EPD_ENABLED
    display_show_logo();
#endif
    break;

  case FBN_CTRL_CMD_REBOOT:
    LOG_INF("BLE requested reboot; scheduling in 500ms");
    k_work_schedule(&delayed_reboot_work, K_MSEC(500));
    break;

  case FBN_CTRL_CMD_STOP_BLE:
    LOG_INF("BLE stop requested via control characteristic");
    ble_manager_stop_advertising();
    break;

  default:
    LOG_WRN("Unknown control command: 0x%02X", cmd);
    return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
  }

  return len;
}

/* --- GATT Service Definition --- */

BT_GATT_SERVICE_DEFINE(fbn_svc,
  BT_GATT_PRIMARY_SERVICE(BT_UUID_FBN_SERVICE),

  /* 1. Configuration Characteristic (Read, Write) */
  BT_GATT_CHARACTERISTIC(BT_UUID_FBN_CONFIG_CHRC,
                         BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                         BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                         read_config, write_config, NULL),

  /* 2. Diagnostics Characteristic (Read, Notify) */
  BT_GATT_CHARACTERISTIC(BT_UUID_FBN_DIAG_CHRC,
                         BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                         BT_GATT_PERM_READ,
                         read_diag, NULL, NULL),
  BT_GATT_CCC(diag_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

  /* 3. Control Characteristic (Write) */
  BT_GATT_CHARACTERISTIC(BT_UUID_FBN_CTRL_CHRC,
                         BT_GATT_CHRC_WRITE,
                         BT_GATT_PERM_WRITE,
                         NULL, write_ctrl, NULL),
);

int ble_fbn_service_init(void) {
  diag_notify_enabled = false;
  LOG_INF("FeedbackNow custom GATT service initialized");
  return 0;
}

int ble_fbn_service_notify_diag(void) {
  if (!diag_notify_enabled) {
    return -EACCES;
  }

  struct fbn_ble_diag_read_resp resp;
  gather_diag_payload(&resp);

  /* fbn_svc.attrs[4] corresponds to the Diagnostics characteristic value attribute */
  return bt_gatt_notify(NULL, &fbn_svc.attrs[4], &resp, sizeof(resp));
}
